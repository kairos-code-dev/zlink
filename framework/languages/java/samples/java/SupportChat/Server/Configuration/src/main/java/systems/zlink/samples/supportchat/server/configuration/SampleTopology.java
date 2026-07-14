package systems.zlink.samples.supportchat.server.configuration;

import org.springframework.boot.context.properties.ConfigurationProperties;
import systems.zlink.contracts.core.RoutingId;

@ConfigurationProperties("sample")
public record SampleTopology(
    String redisEndpoint,
    String redisKeyPrefix,
    String logDirectory,
    String apiChannelEndpoint,
    String apiHttpEndpoint,
    String supportChannelEndpoint,
    String sessionStreamEndpoint,
    String sessionSpotRouterEndpoint,
    String supportSpotRouterEndpoint,
    String sessionSpotNodeRid,
    String supportSpotNodeRid,
    String supportHttpEndpoint) {

    public Location location() {
        return new Location(required(redisEndpoint, "redisEndpoint"), required(redisKeyPrefix, "redisKeyPrefix"));
    }

    public String requiredLogDirectory() {
        return required(logDirectory, "logDirectory");
    }

    public Api api() {
        return new Api(
            required(apiChannelEndpoint, "apiChannelEndpoint"),
            required(apiHttpEndpoint, "apiHttpEndpoint"));
    }

    public Session session() {
        return new Session(
            required(sessionStreamEndpoint, "sessionStreamEndpoint"),
            required(sessionSpotRouterEndpoint, "sessionSpotRouterEndpoint"),
            RoutingId.from(required(sessionSpotNodeRid, "sessionSpotNodeRid")),
            required(supportSpotRouterEndpoint, "supportSpotRouterEndpoint"),
            RoutingId.from(required(supportSpotNodeRid, "supportSpotNodeRid")));
    }

    public Support support() {
        return new Support(
            required(supportChannelEndpoint, "supportChannelEndpoint"),
            required(supportSpotRouterEndpoint, "supportSpotRouterEndpoint"),
            RoutingId.from(required(supportSpotNodeRid, "supportSpotNodeRid")),
            required(sessionSpotRouterEndpoint, "sessionSpotRouterEndpoint"),
            RoutingId.from(required(sessionSpotNodeRid, "sessionSpotNodeRid")),
            required(supportHttpEndpoint, "supportHttpEndpoint"));
    }

    public static String configPath(String[] args) {
        if (args.length != 2 || !"--config".equals(args[0]) || args[1].isBlank()) {
            throw new IllegalArgumentException("Usage: <role executable> --config <path>");
        }
        return args[1];
    }

    private static String required(String value, String name) {
        if (value == null || value.isBlank()) {
            throw new IllegalArgumentException("sample." + name + " is required");
        }
        return value;
    }

    public record Location(String redisEndpoint, String redisKeyPrefix) {
    }

    public record Api(String channelEndpoint, String httpEndpoint) {
    }

    public record Session(
        String streamEndpoint,
        String routerEndpoint,
        RoutingId routingId,
        String peerRouterEndpoint,
        RoutingId peerRoutingId) {
    }

    public record Support(
        String channelEndpoint,
        String routerEndpoint,
        RoutingId routingId,
        String peerRouterEndpoint,
        RoutingId peerRoutingId,
        String httpEndpoint) {
    }
}
