package systems.zlink.samples.gamequest.server.configuration;

import org.springframework.boot.context.properties.ConfigurationProperties;

@ConfigurationProperties("sample")
public record SampleTopology(
    String instanceName,
    String logDirectory,
    String streamEndpoint,
    String channelEndpoint,
    String spotEndpoint,
    String spotRouterEndpoint,
    String notificationChannelEndpoint,
    String apiANotificationChannelEndpoint,
    String apiBNotificationChannelEndpoint,
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
            required(missionBChannelEndpoint, "missionBChannelEndpoint"),
            required(notificationChannelEndpoint, "notificationChannelEndpoint"));
    }

    public QuestMission questMission() {
        return new QuestMission(
            required(instanceName, "instanceName"),
            required(logDirectory, "logDirectory"),
            required(channelEndpoint, "channelEndpoint"),
            required(httpEndpoint, "httpEndpoint"),
            required(spotEndpoint, "spotEndpoint"),
            required(spotRouterEndpoint, "spotRouterEndpoint"),
            required(apiANotificationChannelEndpoint, "apiANotificationChannelEndpoint"),
            required(apiBNotificationChannelEndpoint, "apiBNotificationChannelEndpoint"));
    }

    public Location location() {
        return new Location(
            required(redisEndpoint, "redisEndpoint"),
            required(redisKeyPrefix, "redisKeyPrefix"));
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
        String missionBChannelEndpoint,
        String notificationChannelEndpoint) {
    }

    public record QuestMission(
        String instanceName,
        String logDirectory,
        String channelEndpoint,
        String httpEndpoint,
        String spotEndpoint,
        String spotRouterEndpoint,
        String apiANotificationChannelEndpoint,
        String apiBNotificationChannelEndpoint) {
    }

    public record Location(String redisEndpoint, String redisKeyPrefix) {
    }
}
