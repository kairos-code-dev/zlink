package systems.zlink.e2e.pubsub.publisher.Configuration;

public record PublisherOptions(
    String httpEndpoint,
    String publisherEndpoint,
    String registryRouterEndpoint,
    String logDir) {
    public static PublisherOptions fromEnv() {
        return new PublisherOptions(
            Env.get("ZLINK_JAVA_E2E_PUBLISHER_HTTP"),
            Env.get("ZLINK_JAVA_E2E_PUBLISHER_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_REGISTRY_ROUTER"),
            Env.get("ZLINK_JAVA_E2E_LOG_DIR", "logs"));
    }
}
