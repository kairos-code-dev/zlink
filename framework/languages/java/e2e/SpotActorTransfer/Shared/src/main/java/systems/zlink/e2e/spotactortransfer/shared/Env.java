package systems.zlink.e2e.spotactortransfer.shared;

public final class Env {
    private Env() {
    }

    public static String get(String name, String fallback) {
        String value = System.getenv(name);
        return value == null || value.isBlank() ? fallback : value;
    }

    public static String require(String name) {
        String value = get(name, "");
        if (value.isBlank()) {
            throw new IllegalStateException(name + " is required");
        }
        return value;
    }
}
