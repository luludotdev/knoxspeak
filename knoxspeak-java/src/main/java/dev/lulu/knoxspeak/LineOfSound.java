package dev.lulu.knoxspeak;

import zombie.iso.IsoCell;
import zombie.iso.IsoGridSquare;
import zombie.iso.IsoWorld;
import zombie.iso.LosUtil;

import java.util.HashMap;
import java.util.Map;

final class LineOfSound {
    private static final long REFRESH_MS = 500; // limit cached lookups to at most half a second
    private static final int MAX_CACHE = 128;
    private final Map<Object, Entry> cache = new HashMap<>();

    //region los tracing
    // trace sound path and calculate obstacles
    // worst obstacle wins and determines the muffling
    private static VoiceMath.Path trace(int ax, int ay, int az, int bx, int by, int bz) {
        IsoCell cell = IsoWorld.instance.getCell();
        IsoGridSquare prev = cell.getGridSquare(ax, ay, az);
        VoiceMath.Path worst = VoiceMath.Path.CLEAR;
        int dx = bx - ax, dy = by - ay;
        int steps = Math.max(Math.abs(dx), Math.abs(dy));
        boolean alongX = Math.abs(dx) > Math.abs(dy);
        float m = alongX ? (dx == 0 ? 0f : (float) dy / Math.abs(dx)) : (dy == 0 ? 0f : (float) dx / Math.abs(dy));
        float t = 0.5f + (alongX ? ay : ax);
        int x = ax, y = ay;
        for (int i = 0; i < steps; i++) {
            t += m;
            if (alongX) {
                x += Integer.signum(dx);
                y = (int) Math.floor(t);
            } else {
                y += Integer.signum(dy);
                x = (int) Math.floor(t);
            }

            IsoGridSquare cur = cell.getGridSquare(x, y, az);
            if (cur != null && prev != null) {
                VoiceMath.Path step = stepPath(prev, cur);
                if (step.ordinal() > worst.ordinal()) {
                    worst = step;
                    // never worse than a wall between
                    if (worst == VoiceMath.Path.WALL) return worst;
                }
            }
            prev = cur;
        }
        return worst;
    }

    private static LosUtil.TestResults vision(IsoGridSquare from, IsoGridSquare to, boolean ignoreDoors) {
        return to.testVisionAdjacent(from.getX() - to.getX(), from.getY() - to.getY(), 0, true, ignoreDoors);
    }

    private static VoiceMath.Path stepPath(IsoGridSquare from, IsoGridSquare to) {
        LosUtil.TestResults r = vision(from, to, false);
        return switch (r) {
            case Clear -> VoiceMath.Path.CLEAR;
            case ClearThroughOpenDoor -> VoiceMath.Path.OPEN_DOOR;
            case ClearThroughWindow ->
                VoiceMath.Path.WINDOW; // zomboid doesnt distinguish open/closed windows so assume closed :(
            case ClearThroughClosedDoor -> VoiceMath.Path.CLOSED_DOOR;

            // double check if the blockage was a closed door or a tree
            // solves an edge case and means a sapling doesnt count as a wall lol
            case Blocked -> {
                if (vision(from, to, true) != LosUtil.TestResults.Blocked) {
                    yield VoiceMath.Path.CLOSED_DOOR;
                }

                yield blockedOnlyByTree(from, to) ? VoiceMath.Path.CLEAR : VoiceMath.Path.WALL;
            }
        };
    }
    //endregion

    //region edge case checks
    // check for holes between floors (ie: stairwells) to prevent muffling on stairs
    static boolean openBetweenFloors(IsoGridSquare a, IsoGridSquare b) {
        if (a.HasStairs() || b.HasStairs()) {
            return true;
        }

        LosUtil.TestResults r = LosUtil.lineClear(IsoWorld.instance.getCell(), a.getX(), a.getY(), a.getZ(), b.getX(), b.getY(), b.getZ(), true);
        return r == LosUtil.TestResults.Clear || r == LosUtil.TestResults.ClearThroughOpenDoor;
    }

    private static boolean blockedOnlyByTree(IsoGridSquare from, IsoGridSquare to) {
        if (from.getX() == to.getX() || from.getY() == to.getY()) {
            if (edgeBlocked(from, to)) return false;
            return from.getTree() != null || to.getTree() != null;
        }

        // diagonal-aware
        // zomboid line of sight sucks so bad man
        IsoCell cell = IsoWorld.instance.getCell();
        IsoGridSquare a = cell.getGridSquare(from.getX(), to.getY(), from.getZ());
        IsoGridSquare b = cell.getGridSquare(to.getX(), from.getY(), from.getZ());
        boolean viaA = a != null && !edgeBlocked(from, a) && !edgeBlocked(a, to);
        boolean viaB = b != null && !edgeBlocked(from, b) && !edgeBlocked(b, to);
        if (!viaA && !viaB) return false;

        return from.getTree() != null || to.getTree() != null || (a != null && a.getTree() != null) || (b != null && b.getTree() != null);
    }

    private static boolean edgeBlocked(IsoGridSquare p, IsoGridSquare q) {
        return p.isWallTo(q) || p.isWindowTo(q);
    }
    //endregion

    // cached tracing
    VoiceMath.Path between(int ax, int ay, int az, Object source, int bx, int by, int bz, long now) {
        Entry e = cache.get(source);
        if (e != null && e.ax == ax && e.ay == ay && e.az == az && e.bx == bx && e.by == by && e.bz == bz && now - e.at < REFRESH_MS) {
            // cached fast path
            return e.path;
        }

        VoiceMath.Path path = trace(ax, ay, az, bx, by, bz);
        if (e == null) {
            if (cache.size() >= MAX_CACHE) cache.clear();
            e = new Entry();
            cache.put(source, e);
        }

        e.set(ax, ay, az, bx, by, bz, now, path);
        return path;
    }

    // cache entry
    private static final class Entry {
        int ax, ay, az, bx, by, bz;
        long at;
        VoiceMath.Path path;

        void set(int ax, int ay, int az, int bx, int by, int bz, long at, VoiceMath.Path path) {
            this.ax = ax;
            this.ay = ay;
            this.az = az;
            this.bx = bx;
            this.by = by;
            this.bz = bz;
            this.at = at;
            this.path = path;
        }
    }
}
