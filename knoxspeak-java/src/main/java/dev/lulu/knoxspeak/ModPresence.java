package dev.lulu.knoxspeak;

import zombie.characters.IsoPlayer;
import zombie.network.GameClient;

final class ModPresence {
    static final String MOD_ID = "knoxspeak";
    private static final long CHECK_INTERVAL_MS = 2000;

    private ModPresence() {
    }

    static void start(Bridge bridge) {
        Thread t = new Thread(() -> run(bridge), "knoxspeak-modcheck");
        t.setDaemon(true);
        t.start();
    }

    private static void run(Bridge bridge) {
        boolean logged = false;
        while (true) {
            boolean lacking;
            try {
                lacking = serverLacksMod();
            } catch (RuntimeException e) {
                lacking = false; // e.g. the list changed under us; check again later
            }

            if (lacking && !logged) {
                Log.warn("this server doesn't run knoxspeak (not in mod list); positional voice disabled");
            }
            logged = lacking;
            bridge.setServerLacksMod(lacking);
            try {
                Thread.sleep(CHECK_INTERVAL_MS);
            } catch (InterruptedException e) {
                return;
            }
        }
    }

    private static boolean serverLacksMod() {
        if (!GameClient.client || GameClient.instance == null || IsoPlayer.getInstance() == null) {
            return false;
        }

        return GameClient.instance.serverMods.stream()
            .noneMatch(id -> id != null && MOD_ID.equals(id.replace("\\", "").trim()));
    }
}
