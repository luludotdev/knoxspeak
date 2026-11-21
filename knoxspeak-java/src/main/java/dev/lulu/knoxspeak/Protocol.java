package dev.lulu.knoxspeak;

public final class Protocol {
    public static final int VERSION = 1;
    public static final int DEFAULT_PORT = 47123;

    private Protocol() {
    }

    //region game -> plugin
    private static String line(String... fields) {
        StringBuilder sb = new StringBuilder(64);
        for (int i = 0; i < fields.length; i++) {
            if (i > 0) {
                sb.append('\t');
            }
            appendSanitized(sb, fields[i]);
        }
        return sb.append('\n').toString();
    }

    // announce protocol version, mod version, and current player's username
    public static String hello(String modVersion, String username) {
        return line("HELLO", Integer.toString(VERSION), modVersion, username);
    }

    // announce leaving the session
    public static String bye() {
        return line("BYE");
    }

    // begin a speaking frame
    static StringBuilder appendFrameBegin(StringBuilder sb, int seq) {
        return sb.append("FRAME\t").append(Integer.toUnsignedLong(seq)).append('\n');
    }

    // report audible speaker and their DSP params
    static StringBuilder appendSpeaker(StringBuilder sb, String username, float gain, float muffle, float pan) {
        return speakerFields(sb, username, gain, muffle, pan).append('\n');
    }

    // report audible radio speaker and their DSP params
    static StringBuilder appendRadioSpeaker(StringBuilder sb, String username, float gain, float muffle, float pan, float quality, float pickupMuffle) {
        speakerFields(sb, username, gain, muffle, pan).append("\t1\t");
        appendFixed(sb, Math.clamp(quality, 0, 1), 3).append('\t');
        return appendFixed(sb, Math.clamp(pickupMuffle, 0, 1), 3).append('\n');
    }

    private static StringBuilder speakerFields(StringBuilder sb, String username, float gain, float muffle, float pan) {
        appendSanitized(sb.append("SPK\t"), username).append('\t');
        appendFixed(sb, Math.clamp(gain, 0, 1), 3).append('\t');
        appendFixed(sb, Math.clamp(muffle, 0, 1), 3).append('\t');
        return appendFixed(sb, Math.clamp(pan, -1, 1), 3);
    }

    // end speaking frame
    static StringBuilder appendFrameEnd(StringBuilder sb, int seq) {
        return sb.append("END\t").append(Integer.toUnsignedLong(seq)).append('\n');
    }

    // report config change
    // key: `radiofilter` or `radiostatic`
    public static String config(String key, boolean value) {
        return line("CFG", key, value ? "1" : "0");
    }

    // report carried walkies
    public static String carry(String list) {
        return line("CARRY", list);
    }

    // report muted state
    public static String mute(boolean muted) {
        return line("MUTE", muted ? "1" : "0");
    }

    // report silenced state (dead/invisible)
    public static String silence(boolean silenced) {
        return line("SILENCE", silenced ? "1" : "0");
    }

    // report mod missing on server error
    public static String noMod(boolean lacking) {
        return line("NOMOD", lacking ? "1" : "0");
    }
    //endregion

    //region plugin -> game
    public static PluginMessage parse(String line) {
        if (line == null) return null;
        if (line.endsWith("\r")) line = line.substring(0, line.length() - 1);
        String[] f = line.split("\t", -1);
        try {
            return switch (f[0]) {
                case "HELLO" -> f.length >= 3 ? new Hello(Integer.parseInt(f[1]), f[2]) : null;
                case "TS" -> f.length >= 2 ? new TsStatus("1".equals(f[1])) : null;
                case "TALK" -> f.length >= 3 && !f[1].isEmpty() ? new Talk(f[1], "1".equals(f[2])) : null;
                case "MIC" -> f.length >= 2 ? new Mic("1".equals(f[1])) : null;
                case "PEERCARRY" ->
                    f.length >= 2 && !f[1].isEmpty() ? new PeerCarry(f[1], f.length >= 3 ? f[2] : "") : null;
                case "ERR" -> new Error(f.length >= 2 ? f[1] : "");
                default -> null;
            };
        } catch (NumberFormatException e) {
            return null;
        }
    }

    public sealed interface PluginMessage permits Hello, TsStatus, Talk, Mic, PeerCarry, Error {
    }

    public record PeerCarry(String username, String list) implements PluginMessage {
    }

    public record Hello(int version, String pluginVersion) implements PluginMessage {
    }

    public record TsStatus(boolean connected) implements PluginMessage {
    }

    public record Talk(String username, boolean talking) implements PluginMessage {
    }

    public record Mic(boolean muted) implements PluginMessage {
    }

    public record Error(String message) implements PluginMessage {
    }
    //endregion

    //region utils
    // normalize usernames the same way zomboid does
    public static String normalizeName(String name) {
        if (name == null) {
            return "";
        }
        String s = name.strip();
        StringBuilder sb = new StringBuilder(s.length());
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            sb.append(c >= 'A' && c <= 'Z' ? (char) (c + ('a' - 'A')) : c);
        }
        return sb.toString();
    }

    static StringBuilder appendSanitized(StringBuilder sb, String value) {
        if (value == null) return sb;

        for (int i = 0; i < value.length(); i++) {
            char c = value.charAt(i);
            sb.append(c == '\t' || c == '\n' || c == '\r' ? ' ' : c);
        }
        return sb;
    }

    // optimized (allocation lite) float to string
    static StringBuilder appendFixed(StringBuilder sb, float v, int decimals) {
        int scale = decimals <= 1 ? 10 : decimals == 2 ? 100 : 1000;
        long n = Math.round((double) v * scale);
        if (n < 0) {
            sb.append('-');
            n = -n;
        }
        sb.append(n / scale).append('.');
        long frac = n % scale;
        for (int d = scale / 10; d > 1 && frac < d; d /= 10) {
            sb.append('0');
        }
        return sb.append(frac);
    }
    //endregion
}
