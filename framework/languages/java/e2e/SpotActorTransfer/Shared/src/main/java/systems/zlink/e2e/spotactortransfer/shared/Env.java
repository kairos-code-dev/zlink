package systems.zlink.e2e.spotactortransfer.shared;

public final class Env {
    private Env() {
    }

    public static String require(String name) {
        String value = System.getenv(name);
        if (value == null || value.isBlank()) {
            throw new IllegalStateException("environment variable is required: " + name);
        }
        return value;
    }

    public static String get(String name, String fallback) {
        String value = System.getenv(name);
        return value == null || value.isBlank() ? fallback : value;
    }
}
