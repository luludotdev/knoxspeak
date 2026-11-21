package dev.lulu.knoxspeak;

import se.krka.kahlua.integration.annotations.LuaMethod;

public final class LuaApi {
    private LuaApi() {
    }

    @LuaMethod(name = "KnoxSpeak_onTick", global = true)
    public static void onTick() {
        Main.voiceTick.tick();
    }

    @LuaMethod(name = "KnoxSpeak_isPluginConnected", global = true)
    public static boolean isPluginConnected() {
        return Main.bridge.isConnected();
    }

    @LuaMethod(name = "KnoxSpeak_isTsConnected", global = true)
    public static boolean isTsConnected() {
        return Main.voiceTick.isTsConnected();
    }

    @LuaMethod(name = "KnoxSpeak_isMicMuted", global = true)
    public static boolean isMicMuted() {
        return Main.voiceTick.isMicMuted();
    }

    @LuaMethod(name = "KnoxSpeak_setMicMuted", global = true)
    public static void setMicMuted(boolean muted) {
        Main.voiceTick.setMicMuted(muted);
    }

    @LuaMethod(name = "KnoxSpeak_setClientOptions", global = true)
    public static void setClientOptions(boolean radioFilter, boolean radioStatic, boolean hearingPenalties) {
        Main.voiceTick.setClientOptions(radioFilter, radioStatic, hearingPenalties);
    }

    @LuaMethod(name = "KnoxSpeak_setHearing", global = true)
    public static void setHearing(double voiceRange, double muffleStrength, double falloff) {
        Main.voiceTick.setHearing(voiceRange, muffleStrength, falloff);
    }

    @LuaMethod(name = "KnoxSpeak_getVersion", global = true)
    public static String getVersion() {
        return Main.MOD_VERSION;
    }
}
