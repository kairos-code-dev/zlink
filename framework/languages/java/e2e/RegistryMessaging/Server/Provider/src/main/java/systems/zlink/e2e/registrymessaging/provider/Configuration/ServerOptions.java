package systems.zlink.e2e.registrymessaging.provider.Configuration;

public final class ServerOptions {
    private ServerOptions() {
    }

    public static String get(String name) {
        return get(name, "");
    }

    public static String get(String name, String fallback) {
        String value = System.getenv(name);
        return value == null || value.isBlank() ? fallback : value;
    }
}
