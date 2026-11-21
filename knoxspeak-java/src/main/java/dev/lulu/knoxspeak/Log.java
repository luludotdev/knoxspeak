package dev.lulu.knoxspeak;

final class Log {
    private Log() {
    }

    static void info(String msg) {
        System.out.println("[knoxspeak] " + msg);
    }

    static void warn(String msg) {
        System.out.println("[knoxspeak] WARN " + msg);
    }
}
