package systems.zlink.samples.shoppingmall.server.configuration;

import java.nio.charset.StandardCharsets;
import org.springframework.boot.context.properties.ConfigurationProperties;
import systems.zlink.contracts.core.RoutingId;

@ConfigurationProperties("sample")
public record SampleTopology(
    String instanceName,
    String logDirectory,
    String httpUrl,
    String channelEndpoint,
    String spotEndpoint,
    String spotRouterEndpoint,
    String redisEndpoint,
    String redisKeyPrefix) {

    public Api api() {
        return new Api(required(instanceName, "instanceName"), required(logDirectory, "logDirectory"),
            required(httpUrl, "httpUrl"));
    }

    public Workflow workflow() {
        String name = required(instanceName, "instanceName");
        return new Workflow(name, required(logDirectory, "logDirectory"), required(httpUrl, "httpUrl"),
            required(channelEndpoint, "channelEndpoint"), required(spotEndpoint, "spotEndpoint"),
            required(spotRouterEndpoint, "spotRouterEndpoint"), RoutingId.from(name));
    }

    public Location location() {
        return new Location(required(redisEndpoint, "redisEndpoint"), required(redisKeyPrefix, "redisKeyPrefix"));
    }

    public String ownerWorkflowName(String orderId) {
        int sum = 0;
        for (byte value : orderId.getBytes(StandardCharsets.UTF_8)) sum += Byte.toUnsignedInt(value);
        return sum % 2 == 1 ? "workflow-b" : "workflow-a";
    }

    public static String configPath(String[] args) {
        if (args.length != 2 || !"--config".equals(args[0]) || args[1].isBlank()) {
            throw new IllegalArgumentException("Usage: <role executable> --config <path>");
        }
        return args[1];
    }

    private static String required(String value, String name) {
        if (value == null || value.isBlank()) throw new IllegalArgumentException("sample." + name + " is required");
        return value;
    }

    public record Api(String instanceName, String logDirectory, String httpUrl) { }
    public record Workflow(String instanceName, String logDirectory, String httpUrl, String channelEndpoint,
        String spotEndpoint, String spotRouterEndpoint, RoutingId routingId) { }
    public record Location(String redisEndpoint, String redisKeyPrefix) { }
}
