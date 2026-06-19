package systems.zlink.samples.tictactoe.server.configuration;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.Properties;

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
    String routeEndpoint,
    List<String> routeEndpoints,
    String spotPubSubEndpoint,
    List<String> spotPubSubEndpoints,
    String redisEndpoint,
    String playSpotNodeRid,
    String peerPlaySpotNodeRid,
    String peerSpotEndpoint,
    String peerRouteEndpoint,
    String peerSpotPubSubEndpoint,
    String logDirectory) {
    public static SampleSettings createDefault() {
        return new SampleSettings(
            "http://127.0.0.1:18080",
            "http://127.0.0.1:18080",
            "tcp://127.0.0.1:47201",
            "tcp://127.0.0.1:47203",
            List.of("tcp://127.0.0.1:47203", "tcp://127.0.0.1:47213"),
            "tcp://127.0.0.1:47202",
            List.of("tcp://127.0.0.1:47202", "tcp://127.0.0.1:47212"),
            "tcp://127.0.0.1:47205",
            List.of("tcp://127.0.0.1:47205", "tcp://127.0.0.1:47215"),
            "tcp://127.0.0.1:47206",
            List.of("tcp://127.0.0.1:47206", "tcp://127.0.0.1:47216"),
            "tcp://127.0.0.1:47207",
            List.of("tcp://127.0.0.1:47207", "tcp://127.0.0.1:47217"),
            "127.0.0.1:6379",
            "play-node-1",
            "play-node-2",
            "tcp://127.0.0.1:47215",
            "tcp://127.0.0.1:47216",
            "tcp://127.0.0.1:47217",
            "logs/tictactoe");
    }

    public static SampleSettings load(String[] args) {
        SampleSettings defaults = createDefault();
        SampleSettings configured = fromProperties(readOption(args, "--config", null), defaults);
        return fromArgs(args, configured);
    }

    private static SampleSettings fromArgs(String[] args, SampleSettings defaults) {
        return new SampleSettings(
            readOption(args, "--api-bind", defaults.apiBindUrl()),
            readOption(args, "--api-url", readOption(args, "--api-bind", defaults.apiPublicUrl())),
            readOption(args, "--api-channel-endpoint", defaults.apiChannelEndpoint()),
            readOption(args, "--play-channel-endpoint", defaults.playChannelEndpoint()),
            readListOption(args, "--play-channel-endpoints", defaults.playChannelEndpoints()),
            readOption(args, "--play-endpoint", defaults.playEndpoint()),
            readListOption(args, "--play-endpoints", defaults.playEndpoints()),
            readOption(args, "--spot-endpoint", defaults.spotEndpoint()),
            readListOption(args, "--spot-endpoints", defaults.spotEndpoints()),
            readOption(args, "--route-endpoint", defaults.routeEndpoint()),
            readListOption(args, "--route-endpoints", defaults.routeEndpoints()),
            readOption(args, "--spot-pubsub-endpoint", defaults.spotPubSubEndpoint()),
            readListOption(args, "--spot-pubsub-endpoints", defaults.spotPubSubEndpoints()),
            readOption(args, "--redis-endpoint", defaults.redisEndpoint()),
            readOption(args, "--play-spot-node-rid", defaults.playSpotNodeRid()),
            readOption(args, "--peer-play-spot-node-rid", defaults.peerPlaySpotNodeRid()),
            readOption(args, "--peer-spot-endpoint", defaults.peerSpotEndpoint()),
            readOption(args, "--peer-route-endpoint", defaults.peerRouteEndpoint()),
            readOption(args, "--peer-spot-pubsub-endpoint", defaults.peerSpotPubSubEndpoint()),
            readOption(args, "--log-dir", defaults.logDirectory()));
    }

    private static SampleSettings fromProperties(String path, SampleSettings defaults) {
        if (path == null || path.isBlank()) {
            return defaults;
        }
        Properties properties = new Properties();
        try (var input = Files.newInputStream(Path.of(path))) {
            properties.load(input);
        } catch (IOException error) {
            throw new IllegalArgumentException("Failed to read config file: " + path, error);
        }
        return new SampleSettings(
            properties.getProperty("sample.apiBindUrl", defaults.apiBindUrl()),
            properties.getProperty("sample.apiPublicUrl", defaults.apiPublicUrl()),
            properties.getProperty("sample.apiChannelEndpoint", defaults.apiChannelEndpoint()),
            properties.getProperty("sample.playChannelEndpoint", defaults.playChannelEndpoint()),
            readListProperty(properties, "sample.playChannelEndpoints", defaults.playChannelEndpoints()),
            properties.getProperty("sample.playEndpoint", defaults.playEndpoint()),
            readListProperty(properties, "sample.playEndpoints", defaults.playEndpoints()),
            properties.getProperty("sample.spotEndpoint", defaults.spotEndpoint()),
            readListProperty(properties, "sample.spotEndpoints", defaults.spotEndpoints()),
            properties.getProperty("sample.routeEndpoint", defaults.routeEndpoint()),
            readListProperty(properties, "sample.routeEndpoints", defaults.routeEndpoints()),
            properties.getProperty("sample.spotPubSubEndpoint", defaults.spotPubSubEndpoint()),
            readListProperty(properties, "sample.spotPubSubEndpoints", defaults.spotPubSubEndpoints()),
            properties.getProperty("sample.redisEndpoint", defaults.redisEndpoint()),
            properties.getProperty("sample.playSpotNodeRid", defaults.playSpotNodeRid()),
            properties.getProperty("sample.peerPlaySpotNodeRid", defaults.peerPlaySpotNodeRid()),
            properties.getProperty("sample.peerSpotEndpoint", defaults.peerSpotEndpoint()),
            properties.getProperty("sample.peerRouteEndpoint", defaults.peerRouteEndpoint()),
            properties.getProperty("sample.peerSpotPubSubEndpoint", defaults.peerSpotPubSubEndpoint()),
            properties.getProperty("sample.logDirectory", defaults.logDirectory()));
    }

    public int apiHttpPort() {
        return java.net.URI.create(apiBindUrl).getPort();
    }

    private static String readOption(String[] args, String name, String defaultValue) {
        for (int index = 0; index < args.length; index++) {
            if (!args[index].equals(name)) {
                continue;
            }
            if (index + 1 >= args.length) {
                throw new IllegalArgumentException("Missing value for '" + name + "'.");
            }
            return args[index + 1];
        }
        return defaultValue;
    }

    private static List<String> readListOption(String[] args, String name, List<String> defaultValue) {
        return split(readOption(args, name, null), defaultValue);
    }

    private static List<String> readListProperty(
        Properties properties,
        String name,
        List<String> defaultValue) {
        return split(properties.getProperty(name), defaultValue);
    }

    private static List<String> split(String value, List<String> defaultValue) {
        if (value == null || value.isBlank()) {
            return defaultValue;
        }
        return java.util.Arrays.stream(value.split(","))
            .map(String::trim)
            .filter(item -> !item.isEmpty())
            .toList();
    }
}
