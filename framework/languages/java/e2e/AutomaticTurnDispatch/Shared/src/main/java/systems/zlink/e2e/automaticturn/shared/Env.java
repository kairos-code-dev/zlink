package systems.zlink.e2e.automaticturn.shared;

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
}
