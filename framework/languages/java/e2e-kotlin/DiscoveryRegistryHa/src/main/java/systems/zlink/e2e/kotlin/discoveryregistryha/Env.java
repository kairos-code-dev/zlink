package systems.zlink.e2e.kotlin.discoveryregistryha;

import java.util.Arrays;
import java.util.List;

public final class Env {
    private Env() {
    }

    public static String get(String name) {
        return get(name, "");
    }

    public static String get(String name, String fallback) {
        String value = System.getenv(name);
        if (value == null || value.isBlank()) {
            return fallback;
        }
        return value;
    }

    public static List<String> csv(String name) {
        String value = get(name);
        if (value.isBlank()) {
            return List.of();
        }
        return Arrays.stream(value.split(","))
            .map(String::trim)
            .filter(part -> !part.isBlank())
            .toList();
    }
}
