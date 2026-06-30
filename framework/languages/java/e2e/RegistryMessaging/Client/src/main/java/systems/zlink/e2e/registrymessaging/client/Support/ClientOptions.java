package systems.zlink.e2e.registrymessaging.client.Support;

public final class ClientOptions {
    private ClientOptions() {
    }

    public static String get(String name) {
        return get(name, "");
    }

    public static String get(String name, String fallback) {
        String value = System.getenv(name);
        return value == null || value.isBlank() ? fallback : value;
    }
}
