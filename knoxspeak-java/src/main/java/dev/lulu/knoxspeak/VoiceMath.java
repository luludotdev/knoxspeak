package dev.lulu.knoxspeak;

public final class VoiceMath {
    // near field ranges
    // used to ensure open doors / stairs dont occlude when you're near someone
    static final float NEAR_RANGE = 3f;
    static final float NEAR_FADE_END = 5f;

    private VoiceMath() {
    }

    //region audio effect calculation
    public record Params(float gain, float muffle, float pan) {
    }

    public static Params compute(float dx, float dy, int floors, Occlusion occlusion, Path path, Settings s) {
        return compute(dx, dy, floors, true, occlusion, path, s);
    }

    public static Params compute(float dx, float dy, int floors, boolean floorsMuffle, Occlusion occlusion, Path path, Settings s) {
        floors = Math.abs(floors);
        float dz = floors * s.floorHeight();
        float distance = (float) Math.sqrt(dx * dx + dy * dy + dz * dz);

        float muffle = occlusion.muffle + path.muffle * (1f - occlusion.muffle);
        float floorMuffle = floorsMuffle ? floors * s.floorMuffle() : 0f;
        muffle = clamp01((muffle + floorMuffle) * s.muffleScale());

        float effectiveMax = Math.max(s.fullVolumeRange() + 0.5f, s.maxRange() * (1f - s.muffleRangeLoss() * muffle));
        float gain = 1f - smoothstep(s.fullVolumeRange(), effectiveMax, distance);
        if (s.falloffExponent() != 1f) {
            gain = (float) Math.pow(gain, s.falloffExponent());
        }

        float screenX = (dx - dy) * (float) (1.0 / Math.sqrt(2.0));
        float pan = Math.clamp(screenX / s.panRange(), -1f, 1f);
        return new Params(gain, muffle, pan);
    }
    //endregion

    //region near field
    public static boolean inNearField(float dx, float dy) {
        return dx * dx + dy * dy < NEAR_FADE_END * NEAR_FADE_END;
    }

    public static boolean openOnSameFloor(boolean sameRoom, Path path) {
        return sameRoom || path == Path.CLEAR || path == Path.OPEN_DOOR;
    }

    public static Params nearField(Params p, float dx, float dy, boolean open) {
        if (p == null || p.muffle() <= 0f || !open) {
            return p;
        }

        float horizDist = (float) Math.sqrt(dx * dx + dy * dy);
        if (horizDist >= NEAR_FADE_END) {
            return p;
        }

        float keep = smoothstep(NEAR_RANGE, NEAR_FADE_END, horizDist);
        return new Params(p.gain(), p.muffle() * keep, p.pan());
    }
    //endregion

    //region occlusion
    public enum Occlusion {
        NONE(0.0f),
        LIGHT(0.35f),
        MEDIUM(0.6f),
        HEAVY(0.8f),
        SEVERE(0.92f);

        public final float muffle;

        Occlusion(float muffle) {
            this.muffle = muffle;
        }
    }

    public static Occlusion classify(boolean listenerOutside, boolean speakerOutside, boolean sameBuilding, boolean sameRoom, boolean lineOfSight) {
        if (listenerOutside && speakerOutside) {
            return lineOfSight ? Occlusion.NONE : Occlusion.LIGHT;
        }
        if (sameRoom) {
            return Occlusion.NONE;
        }
        if (sameBuilding) {
            return lineOfSight ? Occlusion.LIGHT : Occlusion.MEDIUM;
        }
        if (listenerOutside != speakerOutside) {
            // open door/window vs wall
            return lineOfSight ? Occlusion.LIGHT : Occlusion.HEAVY;
        }

        // different buildings
        return Occlusion.SEVERE;
    }
    //endregion

    // muffle strength based on path obstacles
    public enum Path {
        CLEAR(0.0f),
        OPEN_DOOR(0.1f),
        WINDOW(0.25f),
        CLOSED_DOOR(0.4f),
        WALL(0.55f);

        public final float muffle;

        Path(float muffle) {
            this.muffle = muffle;
        }

        public boolean seeThrough() {
            return this == CLEAR || this == OPEN_DOOR || this == WINDOW;
        }
    }

    // i thought this would be funny lmao
    public enum Hearing {
        NORMAL, HARD_OF_HEARING, DEAF;

        static final float HOH_RANGE_FACTOR = 0.7f;
        static final float HOH_MUFFLE = 0.35f;
        static final float HOH_RADIO_GAIN = 0.6f;
        static final float DEAF_RANGE = 2f; // same as deaf zombie detection range
        static final float DEAF_MUFFLE = 0.6f;

        public static Hearing of(boolean deaf, boolean hardOfHearing) {
            // i legit dont know if an admin can give you both so work it out manually lol
            return deaf ? DEAF : hardOfHearing ? HARD_OF_HEARING : NORMAL;
        }

        public Settings limit(Settings s) {
            return switch (this) {
                case NORMAL -> s;
                case HARD_OF_HEARING -> s.withRange(s.maxRange() * HOH_RANGE_FACTOR);

                // give deaf players a little handout, set the range to always be really close
                // its still really muffled but they're not completely deaf (unlike vanilla)
                case DEAF -> s.withRange(DEAF_RANGE);
            };
        }

        public Params apply(Params p) {
            float extra = switch (this) {
                case NORMAL -> 0f;
                case HARD_OF_HEARING -> HOH_MUFFLE;
                case DEAF -> DEAF_MUFFLE;
            };

            if (p == null || extra == 0f) return p;
            return new Params(p.gain(), p.muffle() + extra * (1f - p.muffle()), p.pan());
        }

        public float radioGain() {
            return switch (this) {
                case NORMAL -> 1f;
                case HARD_OF_HEARING -> HOH_RADIO_GAIN;
                case DEAF -> 0f;
            };
        }

        public boolean needsSight() {
            return this == DEAF;
        }
    }

    //region audio effect settings
    public record Settings(
        float fullVolumeRange,  // distance at which voice is always full volume
        float maxRange,         // max range for unobstructed voice
        float floorHeight,      // distance that a floor difference adds
        float floorMuffle,      // muffle strength of each floor
        float panRange,         // distance at which panning reaches max strength
        float muffleRangeLoss,  // fraction of max range lost at full strength muffling
        float muffleScale,      // multiplier on all muffling
        float falloffExponent   // shapes the voice falloff curve
    ) {
        public static final Settings DEFAULT = new Settings(3f, 30f, 3f, 0.3f, 10f, 0.6f, 1f, 1f);

        public static float falloffExponent(float strength) {
            float s = clamp01(strength);
            return (float) Math.pow(4.0, 2.0 * s - 1.0);
        }

        public Settings withHearing(float range, float mufflePercent, float falloffStrength) {
            float r = Math.max(5f, range);
            return new Settings(Math.min(fullVolumeRange, r * 0.25f), r, floorHeight, floorMuffle, panRange, muffleRangeLoss, Math.max(0f, mufflePercent / 100f), falloffExponent(falloffStrength));
        }

        public Settings withRange(float range) {
            return new Settings(Math.min(fullVolumeRange, range * 0.25f), range, floorHeight, floorMuffle, panRange, muffleRangeLoss, muffleScale, falloffExponent);
        }
    }
    //endregion

    //region math utils
    static float clamp01(float v) {
        return Math.clamp(v, 0f, 1f);
    }

    static float smoothstep(float edge0, float edge1, float x) {
        float t = clamp01((x - edge0) / (edge1 - edge0));
        return t * t * (3f - 2f * t);
    }

    static float round1(float v) {
        return Math.round(clamp01(v) * 10f) / 10f;
    }
    //endregion
}
