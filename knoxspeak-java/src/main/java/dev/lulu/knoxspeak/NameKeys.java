package dev.lulu.knoxspeak;

import java.util.HashMap;
import java.util.Map;

final class NameKeys {
    private static final int MAX = 256;
    private static final Map<String, String> cache = new HashMap<>();

    private NameKeys() {
    }

    // not thread safe !! (this is fine)
    static String of(String name) {
        if (name == null) return "";
        String key = cache.get(name);
        if (key == null) {
            if (cache.size() >= MAX) cache.clear();
            key = Protocol.normalizeName(name);
            cache.put(name, key);
        }

        return key;
    }
}
