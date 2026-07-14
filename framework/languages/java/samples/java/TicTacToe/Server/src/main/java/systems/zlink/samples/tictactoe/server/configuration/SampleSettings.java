package systems.zlink.samples.tictactoe.server.configuration;

import java.util.List;
import org.springframework.boot.context.properties.ConfigurationProperties;

@ConfigurationProperties("sample")
public record SampleSettings(
    String apiBindUrl,
    String apiPublicUrl,
    String apiChannelEndpoint,
    String playChannelEndpoint,
    List<String> playChannelEndpoints,
    String playEndpoint,
    List<String> playEndpoints,
    String spotEndpoint,
    List<String> spotEndpoints,
    String spotPubSubEndpoint,
    List<String> spotPubSubEndpoints,
    String redisEndpoint,
    String redisKeyPrefix,
    String playSpotNodeRid,
    String peerPlaySpotNodeRid,
    String peerSpotEndpoint,
    String peerSpotPubSubEndpoint,
    String logDirectory) {
    public SampleSettings {
        require(apiBindUrl, "apiBindUrl");
        require(apiPublicUrl, "apiPublicUrl");
        require(apiChannelEndpoint, "apiChannelEndpoint");
        require(playChannelEndpoint, "playChannelEndpoint");
        require(playEndpoint, "playEndpoint");
        require(spotEndpoint, "spotEndpoint");
        require(spotPubSubEndpoint, "spotPubSubEndpoint");
        require(redisEndpoint, "redisEndpoint");
        require(redisKeyPrefix, "redisKeyPrefix");
        require(playSpotNodeRid, "playSpotNodeRid");
        require(logDirectory, "logDirectory");
    }

    public int apiHttpPort() {
        return java.net.URI.create(apiBindUrl).getPort();
    }

    public int playIndex() {
        int index = playChannelEndpoints.indexOf(playChannelEndpoint);
        return index >= 0 ? index : 0;
    }

    public String routeEndpoint() {
        return spotEndpoint;
    }

    public static String configPath(String[] args) {
        if (args.length != 2 || !"--config".equals(args[0]) || args[1].isBlank()) {
            throw new IllegalArgumentException("Usage: <role executable> --config <path>");
        }
        return args[1];
    }

    private static void require(String value, String name) {
        if (value == null || value.isBlank()) {
            throw new IllegalArgumentException("sample." + name + " is required");
        }
    }
}
