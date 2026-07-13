package systems.zlink.e2e.kotlin.automaticturn;

public final class Env {
    private Env() {
    }

    public static String get(String name) {
        String value = System.getenv(name);
        if (value == null || value.isBlank()) {
            throw new IllegalStateException("missing environment variable: " + name);
        }
        return value;
    }

    public static String get(String name, String fallback) {
        String value = System.getenv(name);
        return value == null || value.isBlank() ? fallback : value;
    }
}
