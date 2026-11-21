package dev.lulu.knoxspeak;

import java.lang.reflect.Field;

final class VanillaVoice {
    private static final String CLASS = "zombie.core.raknet.VoiceManager";
    private static final String FIELD = "serverVOIPEnable";

    private static Field field;
    private static boolean unavailable;

    private VanillaVoice() {
    }

    // force disable vanilla voip path
    static void suppress() {
        if (unavailable) return;

        try {
            if (field == null) {
                field = Class.forName(CLASS).getDeclaredField(FIELD);
                field.setAccessible(true);
                Log.info("vanilla voice chat disabled on this client (TeamSpeak replaces it)");
            }
            if (field.getBoolean(null)) {
                field.setBoolean(null, false);
            }
        } catch (ReflectiveOperationException | RuntimeException e) {
            unavailable = true; // probably due to a game update
            Log.warn("couldn't disable vanilla voice chat (" + e + "); set VoiceEnable=false on the server");
        }
    }
}
