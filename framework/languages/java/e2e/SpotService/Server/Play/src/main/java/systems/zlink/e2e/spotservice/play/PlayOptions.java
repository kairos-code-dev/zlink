package systems.zlink.e2e.spotservice.play;

import org.springframework.boot.context.properties.ConfigurationProperties;

@ConfigurationProperties("e2e")
public record PlayOptions(
    String nodeRid, String routeEndpoint, String routeAEndpoint, String routeBEndpoint,
    String ingressEndpoint, String ingressAEndpoint, String ingressBEndpoint,
    String spotEndpoint, String spotPubEndpoint, String streamEndpoint,
    String tlsStreamEndpoint, String tlsCertificatePath, String tlsKeyPath,
    String httpEndpoint, String redisLocationEndpoint, String locationKeyPrefix, String logDir) {
    public PlayOptions {
        required(nodeRid, "node-rid"); required(routeEndpoint, "route-endpoint");
        required(routeAEndpoint, "route-a-endpoint"); required(routeBEndpoint, "route-b-endpoint");
        required(ingressEndpoint, "ingress-endpoint"); required(ingressAEndpoint, "ingress-a-endpoint");
        required(ingressBEndpoint, "ingress-b-endpoint"); required(spotEndpoint, "spot-endpoint");
        required(spotPubEndpoint, "spot-pub-endpoint"); required(httpEndpoint, "http-endpoint");
        required(redisLocationEndpoint, "redis-location-endpoint"); required(locationKeyPrefix, "location-key-prefix");
        required(logDir, "log-dir");
        streamEndpoint = optional(streamEndpoint); tlsStreamEndpoint = optional(tlsStreamEndpoint);
        tlsCertificatePath = optional(tlsCertificatePath); tlsKeyPath = optional(tlsKeyPath);
        if (!tlsStreamEndpoint.isBlank() && (tlsCertificatePath.isBlank() || tlsKeyPath.isBlank())) {
            throw new IllegalArgumentException("TLS certificate and key paths are required for e2e.tls-stream-endpoint");
        }
    }
    private static String optional(String value) { return value == null ? "" : value; }
    private static void required(String value, String name) {
        if (value == null || value.isBlank()) throw new IllegalArgumentException("e2e." + name + " is required");
    }
}
