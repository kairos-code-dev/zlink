package systems.zlink.e2e.pubsub.registry.Configuration;

public record RegistryOptions(
    String pubEndpoint,
    String routerEndpoint,
    int httpPort) {
    public static RegistryOptions fromEnv() {
        return new RegistryOptions(
            Env.get("ZLINK_JAVA_E2E_REGISTRY_PUB"),
            Env.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"),
            Integer.parseInt(Env.get("ZLINK_JAVA_E2E_HTTP_PORT", "0")));
    }
}
