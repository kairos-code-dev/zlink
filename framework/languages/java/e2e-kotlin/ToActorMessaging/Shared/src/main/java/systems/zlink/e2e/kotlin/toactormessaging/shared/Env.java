package systems.zlink.e2e.kotlin.toactormessaging.shared;

public final class Env {
    private Env() {
    }

    public static String get(String key) {
        String value = System.getenv(key);
        if (value == null || value.isBlank()) {
            throw new IllegalStateException("Missing environment variable " + key);
        }
        return value;
    }

    public static String get(String key, String fallback) {
        String value = System.getenv(key);
        return value == null || value.isBlank() ? fallback : value;
    }
}
