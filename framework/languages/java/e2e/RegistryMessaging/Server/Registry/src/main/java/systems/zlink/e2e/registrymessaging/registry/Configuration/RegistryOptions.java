package systems.zlink.e2e.registrymessaging.registry.Configuration;

public final class RegistryOptions {
    private RegistryOptions() {
    }

    public static String get(String name) {
        String value = System.getenv(name);
        return value == null ? "" : value;
    }
}
