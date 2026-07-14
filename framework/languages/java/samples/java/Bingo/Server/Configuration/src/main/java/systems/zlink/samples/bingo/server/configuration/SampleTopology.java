package systems.zlink.samples.bingo.server.configuration;

import java.io.Reader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Properties;

public final class SampleTopology {
    public static String ApiAChannelEndpoint;
    public static String ApiBChannelEndpoint;
    public static String PlayAChannelEndpoint;
    public static String PlayBChannelEndpoint;
    public static String SessionASpotEndpoint;
    public static String SessionBSpotEndpoint;
    public static String SessionARouterEndpoint;
    public static String SessionBRouterEndpoint;
    public static String PlayASpotEndpoint;
    public static String PlayBSpotEndpoint;
    public static String PlayASpotRouterEndpoint;
    public static String PlayBSpotRouterEndpoint;
    public static String SessionAStreamEndpoint;
    public static String SessionBStreamEndpoint;
    public static String RedisEndpoint;
    public static String RedisKeyPrefix;
    public static String ApiNode;
    public static String PlayNode;
    public static String SessionNode;
    public static String SessionARouterRid;
    public static String SessionBRouterRid;
    public static String PlayANodeRid;
    public static String PlayBNodeRid;
    public static String PlayRid;
    public static String LogDirectory;

    private SampleTopology() {
    }

    public static void configure(String[] args) {
        Properties properties = load(args);
        ApiAChannelEndpoint = value(properties, "apiAChannelEndpoint", "tcp://127.0.0.1:47103");
        ApiBChannelEndpoint = value(properties, "apiBChannelEndpoint", "tcp://127.0.0.1:47117");
        PlayAChannelEndpoint = value(properties, "playAChannelEndpoint", "tcp://127.0.0.1:47104");
        PlayBChannelEndpoint = value(properties, "playBChannelEndpoint", "tcp://127.0.0.1:47118");
        SessionASpotEndpoint = value(properties, "sessionASpotEndpoint", "tcp://127.0.0.1:47105");
        SessionBSpotEndpoint = value(properties, "sessionBSpotEndpoint", "tcp://127.0.0.1:47119");
        SessionARouterEndpoint = value(properties, "sessionARouterEndpoint", "tcp://127.0.0.1:47106");
        SessionBRouterEndpoint = value(properties, "sessionBRouterEndpoint", "tcp://127.0.0.1:47120");
        PlayASpotEndpoint = value(properties, "playASpotEndpoint", "tcp://127.0.0.1:47110");
        PlayBSpotEndpoint = value(properties, "playBSpotEndpoint", "tcp://127.0.0.1:47121");
        PlayASpotRouterEndpoint = value(properties, "playASpotRouterEndpoint", "tcp://127.0.0.1:47111");
        PlayBSpotRouterEndpoint = value(properties, "playBSpotRouterEndpoint", "tcp://127.0.0.1:47122");
        SessionAStreamEndpoint = value(properties, "sessionAStreamEndpoint", "tcp://127.0.0.1:47114");
        SessionBStreamEndpoint = value(properties, "sessionBStreamEndpoint", "tcp://127.0.0.1:47125");
        RedisEndpoint = required(properties, "redisEndpoint");
        RedisKeyPrefix = value(properties, "redisKeyPrefix", "bingo:java:");
        ApiNode = value(properties, "apiNode", "a");
        PlayNode = value(properties, "playNode", "a");
        SessionNode = value(properties, "sessionNode", "a");
        SessionARouterRid = value(properties, "sessionARouterRid", "1101");
        SessionBRouterRid = value(properties, "sessionBRouterRid", "1102");
        PlayANodeRid = value(properties, "playANodeRid", "2201");
        PlayBNodeRid = value(properties, "playBNodeRid", "2202");
        PlayRid = value(properties, "playRid", PlayBNodeRid);
        LogDirectory = required(properties, "logDirectory");
    }

    public static String selectedApiChannelEndpoint() {
        return "b".equals(ApiNode) ? ApiBChannelEndpoint : ApiAChannelEndpoint;
    }

    public static String selectedPlayChannelEndpoint() {
        return "b".equals(PlayNode) ? PlayBChannelEndpoint : PlayAChannelEndpoint;
    }

    public static String selectedPlaySpotEndpoint() {
        return "b".equals(PlayNode) ? PlayBSpotEndpoint : PlayASpotEndpoint;
    }

    public static String peerPlaySpotEndpoint() {
        return "b".equals(PlayNode) ? PlayASpotEndpoint : PlayBSpotEndpoint;
    }

    public static String selectedPlaySpotRouterEndpoint() {
        return "b".equals(PlayNode) ? PlayBSpotRouterEndpoint : PlayASpotRouterEndpoint;
    }

    public static String preferredPlaySpotRouterEndpoint() {
        return "b".equals(SessionNode) ? PlayBSpotRouterEndpoint : PlayASpotRouterEndpoint;
    }

    public static String selectedPlayNodeRid() {
        return "b".equals(PlayNode) ? PlayBNodeRid : PlayANodeRid;
    }

    public static String preferredPlayNodeRid() {
        return "b".equals(SessionNode) ? PlayBNodeRid : PlayANodeRid;
    }

    public static String selectedSessionSpotEndpoint() {
        return "b".equals(SessionNode) ? SessionBSpotEndpoint : SessionASpotEndpoint;
    }

    public static String selectedSessionRouterEndpoint() {
        return "b".equals(SessionNode) ? SessionBRouterEndpoint : SessionARouterEndpoint;
    }

    public static String selectedSessionRouterRid() {
        return "b".equals(SessionNode) ? SessionBRouterRid : SessionARouterRid;
    }

    public static String selectedStreamEndpoint() {
        return "b".equals(SessionNode) ? SessionBStreamEndpoint : SessionAStreamEndpoint;
    }

    private static Properties load(String[] args) {
        if (args.length != 2 || !"--config".equals(args[0]) || args[1].isBlank()) {
            throw new IllegalArgumentException("Usage: <role> --config <path>");
        }
        Properties properties = new Properties();
        try (Reader reader = Files.newBufferedReader(Path.of(args[1]))) {
            properties.load(reader);
            return properties;
        } catch (Exception error) {
            throw new IllegalStateException("Could not load Bingo sample config.", error);
        }
    }

    private static String value(Properties properties, String name, String fallback) {
        String value = properties.getProperty(name);
        return value == null || value.isBlank() ? fallback : value;
    }

    private static String required(Properties properties, String name) {
        String value = properties.getProperty(name);
        if (value == null || value.isBlank()) {
            throw new IllegalArgumentException("Missing Bingo sample config: " + name);
        }
        return value;
    }
}
