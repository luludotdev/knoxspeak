package dev.lulu.knoxspeak;

public final class Main {
    static final String MOD_VERSION = versionFromManifest();

    static final Bridge bridge = new Bridge(Protocol.DEFAULT_PORT, MOD_VERSION);
    static final VoiceTick voiceTick = new VoiceTick(bridge);
    private static boolean started;

    private Main() {
    }

    @SuppressWarnings("unused")
    public static void main(String[] args) {
        if (started) return;
        started = true;

        bridge.start();
        ModPresence.start(bridge);
        Runtime.getRuntime().addShutdownHook(new Thread(bridge::stop, "knoxspeak-shutdown"));
        Log.info("loaded v" + MOD_VERSION);
    }

    private static String versionFromManifest() {
        String v = Main.class.getPackage().getImplementationVersion();
        return v != null ? v : "dev";
    }
}
