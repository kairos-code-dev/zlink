package systems.zlink.samples.gamequest.server.configuration;

import java.nio.charset.StandardCharsets;
import org.springframework.boot.context.properties.ConfigurationProperties;
import systems.zlink.contracts.core.RoutingId;

@ConfigurationProperties("sample")
public record SampleTopology(
    String instanceName,
    String logDirectory,
    String streamEndpoint,
    String channelEndpoint,
    String spotEndpoint,
    String spotRouterEndpoint,
    String httpEndpoint,
    String missionAChannelEndpoint,
    String missionBChannelEndpoint,
    String redisEndpoint,
    String redisKeyPrefix) {

    public GameApi gameApi() {
        return new GameApi(
            required(instanceName, "instanceName"),
            required(logDirectory, "logDirectory"),
            required(streamEndpoint, "streamEndpoint"),
            required(httpEndpoint, "httpEndpoint"),
            required(missionAChannelEndpoint, "missionAChannelEndpoint"),
            required(missionBChannelEndpoint, "missionBChannelEndpoint"));
    }

    public QuestMission questMission() {
        return new QuestMission(
            required(instanceName, "instanceName"),
            required(logDirectory, "logDirectory"),
            required(channelEndpoint, "channelEndpoint"),
            required(httpEndpoint, "httpEndpoint"),
            required(spotEndpoint, "spotEndpoint"),
            required(spotRouterEndpoint, "spotRouterEndpoint"),
            RoutingId.from(instanceName));
    }

    public Location location() {
        return new Location(
            required(redisEndpoint, "redisEndpoint"),
            required(redisKeyPrefix, "redisKeyPrefix"));
    }

    public String ownerMissionName(String playerId) {
        int sum = 0;
        for (byte value : playerId.getBytes(StandardCharsets.UTF_8)) {
            sum += Byte.toUnsignedInt(value);
        }
        return sum % 2 == 1 ? "mission-b" : "mission-a";
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

    public record GameApi(
        String instanceName,
        String logDirectory,
        String streamEndpoint,
        String httpEndpoint,
        String missionAChannelEndpoint,
        String missionBChannelEndpoint) {
    }

    public record QuestMission(
        String instanceName,
        String logDirectory,
        String channelEndpoint,
        String httpEndpoint,
        String spotEndpoint,
        String spotRouterEndpoint,
        RoutingId routingId) {
    }

    public record Location(String redisEndpoint, String redisKeyPrefix) {
    }
}
