package systems.zlink.e2e.registrymessaging.provider.Configuration;

import org.springframework.boot.context.properties.ConfigurationProperties;

@ConfigurationProperties("e2e")
public record ServerOptions(
    String providerRid,
    String providerInstance,
    String apiWeight,
    long maxMessageSize,
    String apiEndpoint,
    String apiManualEndpoint,
    String routeEndpoint,
    String routePeers,
    String workflowEndpoint,
    int httpPort,
    String redisLocationEndpoint,
    String locationKeyPrefix,
    String logDir) {
    public ServerOptions {
        required(providerRid, "provider-rid");
        required(providerInstance, "provider-instance");
        required(redisLocationEndpoint, "redis-location-endpoint");
        required(locationKeyPrefix, "location-key-prefix");
        required(logDir, "log-dir");
        apiWeight = optional(apiWeight);
        apiEndpoint = optional(apiEndpoint);
        apiManualEndpoint = optional(apiManualEndpoint);
        routeEndpoint = optional(routeEndpoint);
        routePeers = optional(routePeers);
        workflowEndpoint = optional(workflowEndpoint);
    }

    private static String optional(String value) { return value == null ? "" : value; }
    private static void required(String value, String name) {
        if (value == null || value.isBlank()) throw new IllegalArgumentException("e2e." + name + " is required");
    }
}
