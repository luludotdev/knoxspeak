package dev.lulu.knoxspeak;

import dev.lulu.knoxspeak.VoiceMath.Occlusion;
import dev.lulu.knoxspeak.VoiceMath.Params;
import dev.lulu.knoxspeak.VoiceMath.Path;
import dev.lulu.knoxspeak.VoiceMath.Settings;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

class VoiceMathTest {
    private static final Settings S = Settings.DEFAULT;
    private static final double delta = 1e-6;

    @Test
    void fullVolumeUpClose() {
        var p = VoiceMath.compute(1, 1, 0, Occlusion.NONE, Path.CLEAR, S);
        assertEquals(1f, p.gain(), delta);
        assertEquals(0f, p.muffle(), delta);
    }

    @Test
    void silentPastMaxRange() {
        assertEquals(0f, VoiceMath.compute(S.maxRange() + 1, 0, 0, Occlusion.NONE, Path.CLEAR, S).gain(), delta);
    }

    @Test
    void gainFallsWithDistance() {
        float near = VoiceMath.compute(8, 0, 0, Occlusion.NONE, Path.CLEAR, S).gain();
        float far = VoiceMath.compute(20, 0, 0, Occlusion.NONE, Path.CLEAR, S).gain();
        assertTrue(near > far && far > 0);
    }

    @Test
    void occlusionMufflesAndShortensRange() {
        var clear = VoiceMath.compute(10, 0, 0, Occlusion.NONE, Path.CLEAR, S);
        var walled = VoiceMath.compute(10, 0, 0, Occlusion.HEAVY, Path.CLEAR, S);
        assertTrue(walled.muffle() > clear.muffle());
        assertTrue(walled.gain() < clear.gain());
    }

    @Test
    void floorsAddDistanceAndMuffle() {
        var same = VoiceMath.compute(2, 0, 0, Occlusion.NONE, Path.CLEAR, S);
        var above = VoiceMath.compute(2, 0, 1, Occlusion.NONE, Path.CLEAR, S);
        assertTrue(above.muffle() > same.muffle());
        assertTrue(above.gain() <= same.gain());
    }

    @Test
    void panFollowsIsometricScreen() {
        // +x -y is screen right
        // +x +y together is straight down the screen.
        assertTrue(VoiceMath.compute(5, -5, 0, Occlusion.NONE, Path.CLEAR, S).pan() > 0.5f);
        assertTrue(VoiceMath.compute(-5, 5, 0, Occlusion.NONE, Path.CLEAR, S).pan() < -0.5f);
        assertEquals(0f, VoiceMath.compute(5, 5, 0, Occlusion.NONE, Path.CLEAR, S).pan(), delta);
    }

    // mirror of VoiceTick.fromSource without any references to game state
    private static Params sameFloor(float dx, boolean meOutside, boolean theyOutside, boolean sameBuilding,
                                    boolean sameRoom, Path path) {
        Occlusion occ = VoiceMath.classify(meOutside, theyOutside, sameBuilding, sameRoom, path.seeThrough());
        Params p = VoiceMath.compute(dx, 0, 0, !(meOutside && theyOutside), occ, path, S);
        return VoiceMath.nearField(p, dx, 0, VoiceMath.openOnSameFloor(sameRoom, path));
    }

    private static Params otherRoom(float dx, Path path) {
        return sameFloor(dx, false, false, true, false, path);
    }

    //region floors
    @Test
    void bothOutsideDifferentFloorOnlyAddsDistance() {
        // Yard to roof: nothing solid between you, so no floor muffle; the height still counts as distance.
        Occlusion occ = VoiceMath.classify(true, true, false, false, true);
        var roof = VoiceMath.compute(8, 0, 1, false, occ, Path.CLEAR, S);
        var ground = VoiceMath.compute(8, 0, 0, false, occ, Path.CLEAR, S);
        assertEquals(0f, roof.muffle(), delta);
        assertTrue(roof.gain() < ground.gain());
    }

    @Test
    void bothOutsideDifferentFloorWithoutSightIsOnlyLightlyMuffled() {
        Occlusion occ = VoiceMath.classify(true, true, false, false, false);
        assertEquals(Occlusion.LIGHT.muffle, VoiceMath.compute(8, 0, 1, false, occ, Path.CLEAR, S).muffle(), delta);
    }

    @Test
    void floorMuffleStillAppliesIndoors() {
        // Same building, one floor up, can't see each other: the class plus one floor.
        Occlusion occ = VoiceMath.classify(false, false, true, false, false);
        var p = VoiceMath.compute(8, 0, 1, true, occ, Path.CLEAR, S);
        assertEquals(Occlusion.MEDIUM.muffle + S.floorMuffle(), p.muffle(), delta);
    }

    @Test
    void stairwellUpCloseIsClear() {
        // One floor apart through a stairwell (VoiceTick's between-floors trace says open).
        Occlusion occ = VoiceMath.classify(false, false, true, false, false);
        var p = VoiceMath.compute(2, 0, 1, true, occ, Path.CLEAR, S);
        assertEquals(0f, VoiceMath.nearField(p, 2, 0, true).muffle(), delta);
        assertEquals(p.muffle(), VoiceMath.nearField(p, 2, 0, false).muffle(), delta); // solid floor
    }
    //endregion

    //region doors, windows and walls (same floor)
    @Test
    void openDoorFarIsLightlyMuffled() {
        float expected = Occlusion.LIGHT.muffle + Path.OPEN_DOOR.muffle * (1f - Occlusion.LIGHT.muffle);
        assertEquals(expected, otherRoom(8, Path.OPEN_DOOR).muffle(), delta);
    }

    @Test
    void openDoorUpCloseIsClear() {
        assertEquals(0f, otherRoom(2, Path.OPEN_DOOR).muffle(), delta);
        assertEquals(0f, otherRoom(VoiceMath.NEAR_RANGE, Path.OPEN_DOOR).muffle(), delta);
    }

    @Test
    void openDoorMuffleFadesBackIn() {
        float far = otherRoom(8, Path.OPEN_DOOR).muffle();
        float mid = otherRoom((VoiceMath.NEAR_RANGE + VoiceMath.NEAR_FADE_END) / 2, Path.OPEN_DOOR).muffle();
        assertEquals(far / 2, mid, delta); // smoothstep is 0.5 halfway
        assertEquals(far, otherRoom(VoiceMath.NEAR_FADE_END, Path.OPEN_DOOR).muffle(), delta);
    }

    @Test
    void closedDoorFarIsMuffledAndQuieter() {
        var closed = otherRoom(8, Path.CLOSED_DOOR);
        var open = otherRoom(8, Path.OPEN_DOOR);
        // No line of sight through a closed door, so the class is MEDIUM rather than LIGHT.
        float expected = Occlusion.MEDIUM.muffle + Path.CLOSED_DOOR.muffle * (1f - Occlusion.MEDIUM.muffle);
        assertEquals(expected, closed.muffle(), delta);
        assertTrue(closed.gain() < open.gain());
    }

    @Test
    void closedDoorUpCloseStaysMuffled() {
        assertEquals(otherRoom(8, Path.CLOSED_DOOR).muffle(), otherRoom(1, Path.CLOSED_DOOR).muffle(), delta);
    }

    @Test
    void windowSeesThroughButStaysMuffledUpClose() {
        var far = otherRoom(8, Path.WINDOW);
        float expected = Occlusion.LIGHT.muffle + Path.WINDOW.muffle * (1f - Occlusion.LIGHT.muffle);
        assertEquals(expected, far.muffle(), delta);
        assertEquals(far.muffle(), otherRoom(1, Path.WINDOW).muffle(), delta);
    }

    @Test
    void mufflingOrderedByObstacle() {
        float open = otherRoom(8, Path.OPEN_DOOR).muffle();
        float window = otherRoom(8, Path.WINDOW).muffle();
        float closed = otherRoom(8, Path.CLOSED_DOOR).muffle();
        float wall = otherRoom(8, Path.WALL).muffle();
        assertTrue(open < window && window < closed && closed < wall);
    }

    @Test
    void sameRoomIsClear() {
        assertEquals(0f, sameFloor(8, false, false, true, true, Path.CLEAR).muffle(), delta);
    }

    @Test
    void outsideToInsideThroughOpenDoor() {
        // Outside squares have no building, so this is the "one in, one out" class.
        float expected = Occlusion.LIGHT.muffle + Path.OPEN_DOOR.muffle * (1f - Occlusion.LIGHT.muffle);
        assertEquals(expected, sameFloor(8, true, false, false, false, Path.OPEN_DOOR).muffle(), delta);
        assertEquals(0f, sameFloor(2, true, false, false, false, Path.OPEN_DOOR).muffle(), delta);
    }

    @Test
    void outsideToInsideThroughWallIsHeavy() {
        float expected = Occlusion.HEAVY.muffle + Path.WALL.muffle * (1f - Occlusion.HEAVY.muffle);
        var p = sameFloor(2, true, false, false, false, Path.WALL);
        assertEquals(expected, p.muffle(), delta); // up close too
    }

    @Test
    void bothOutsideBlockedByWallIsLight() {
        // e.g. a fence or a building corner between two people outside: LIGHT plus the wall on top.
        float expected = Occlusion.LIGHT.muffle + Path.WALL.muffle * (1f - Occlusion.LIGHT.muffle);
        assertEquals(expected, sameFloor(8, true, true, false, false, Path.WALL).muffle(), delta);
    }
    //endregion

    //region limits and options
    @Test
    void muffleNeverExceedsOne() {
        var p = VoiceMath.compute(10, 0, 3, true, Occlusion.SEVERE, Path.WALL, S);
        assertEquals(1f, p.muffle(), delta);
    }

    @Test
    void muffleStrengthZeroRemovesAllMuffling() {
        var s = S.withHearing(S.maxRange(), 0f, 0.5f);
        assertEquals(0f, VoiceMath.compute(8, 0, 2, true, Occlusion.SEVERE, Path.WALL, s).muffle(), delta);
    }

    @Test
    void hardOfHearingMuffleSurvivesNearField() {
        var p = VoiceMath.Hearing.HARD_OF_HEARING.apply(otherRoom(2, Path.OPEN_DOOR));
        assertEquals(VoiceMath.Hearing.HOH_MUFFLE, p.muffle(), delta);
    }
    //endregion

    @Test
    void classification() {
        assertEquals(Occlusion.NONE, VoiceMath.classify(true, true, false, false, true));
        assertEquals(Occlusion.LIGHT, VoiceMath.classify(true, true, false, false, false));
        assertEquals(Occlusion.NONE, VoiceMath.classify(false, false, true, true, false));
        assertEquals(Occlusion.MEDIUM, VoiceMath.classify(false, false, true, false, false));
        assertEquals(Occlusion.HEAVY, VoiceMath.classify(true, false, false, false, false));
        assertEquals(Occlusion.LIGHT, VoiceMath.classify(true, false, false, false, true));
        assertEquals(Occlusion.SEVERE, VoiceMath.classify(false, false, false, false, false));
    }
}
