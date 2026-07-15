package systems.zlink.samples.bingo.server.configuration;

import org.springframework.boot.context.properties.ConfigurationProperties;

@ConfigurationProperties("sample")
public record SampleTopology(
    String apiAChannelEndpoint,
    String apiBChannelEndpoint,
    String playAChannelEndpoint,
    String playBChannelEndpoint,
    String sessionASpotEndpoint,
    String sessionBSpotEndpoint,
    String sessionARouterEndpoint,
    String sessionBRouterEndpoint,
    String playASpotEndpoint,
    String playBSpotEndpoint,
    String playASpotRouterEndpoint,
    String playBSpotRouterEndpoint,
    String sessionAStreamEndpoint,
    String sessionBStreamEndpoint,
    String redisEndpoint,
    String redisKeyPrefix,
    String apiNode,
    String playNode,
    String sessionNode,
    String sessionARouterRid,
    String sessionBRouterRid,
    String playANodeRid,
    String playBNodeRid,
    String logDirectory) {

    public SampleTopology {
        apiAChannelEndpoint = value(apiAChannelEndpoint, "tcp://127.0.0.1:47103");
        apiBChannelEndpoint = value(apiBChannelEndpoint, "tcp://127.0.0.1:47117");
        playAChannelEndpoint = value(playAChannelEndpoint, "tcp://127.0.0.1:47104");
        playBChannelEndpoint = value(playBChannelEndpoint, "tcp://127.0.0.1:47118");
        sessionASpotEndpoint = value(sessionASpotEndpoint, "tcp://127.0.0.1:47105");
        sessionBSpotEndpoint = value(sessionBSpotEndpoint, "tcp://127.0.0.1:47119");
        sessionARouterEndpoint = value(sessionARouterEndpoint, "tcp://127.0.0.1:47106");
        sessionBRouterEndpoint = value(sessionBRouterEndpoint, "tcp://127.0.0.1:47120");
        playASpotEndpoint = value(playASpotEndpoint, "tcp://127.0.0.1:47110");
        playBSpotEndpoint = value(playBSpotEndpoint, "tcp://127.0.0.1:47121");
        playASpotRouterEndpoint = value(playASpotRouterEndpoint, "tcp://127.0.0.1:47111");
        playBSpotRouterEndpoint = value(playBSpotRouterEndpoint, "tcp://127.0.0.1:47122");
        sessionAStreamEndpoint = value(sessionAStreamEndpoint, "tcp://127.0.0.1:47114");
        sessionBStreamEndpoint = value(sessionBStreamEndpoint, "tcp://127.0.0.1:47125");
        redisEndpoint = required(redisEndpoint, "redisEndpoint");
        redisKeyPrefix = value(redisKeyPrefix, "bingo:java:");
        apiNode = value(apiNode, "a");
        playNode = value(playNode, "a");
        sessionNode = value(sessionNode, "a");
        sessionARouterRid = value(sessionARouterRid, "1101");
        sessionBRouterRid = value(sessionBRouterRid, "1102");
        playANodeRid = value(playANodeRid, "2201");
        playBNodeRid = value(playBNodeRid, "2202");
        logDirectory = required(logDirectory, "logDirectory");
    }

    public String selectedApiChannelEndpoint() {
        return "b".equals(apiNode) ? apiBChannelEndpoint : apiAChannelEndpoint;
    }

    public String selectedPlayChannelEndpoint() {
        return "b".equals(playNode) ? playBChannelEndpoint : playAChannelEndpoint;
    }

    public String selectedPlaySpotEndpoint() {
        return "b".equals(playNode) ? playBSpotEndpoint : playASpotEndpoint;
    }

    public String selectedPlaySpotRouterEndpoint() {
        return "b".equals(playNode) ? playBSpotRouterEndpoint : playASpotRouterEndpoint;
    }

    public String selectedPlayNodeRid() {
        return "b".equals(playNode) ? playBNodeRid : playANodeRid;
    }

    public String preferredPlayNodeRid() {
        return "b".equals(sessionNode) ? playBNodeRid : playANodeRid;
    }

    public String selectedSessionSpotEndpoint() {
        return "b".equals(sessionNode) ? sessionBSpotEndpoint : sessionASpotEndpoint;
    }

    public String selectedSessionRouterEndpoint() {
        return "b".equals(sessionNode) ? sessionBRouterEndpoint : sessionARouterEndpoint;
    }

    public String selectedSessionRouterRid() {
        return "b".equals(sessionNode) ? sessionBRouterRid : sessionARouterRid;
    }

    public String selectedStreamEndpoint() {
        return "b".equals(sessionNode) ? sessionBStreamEndpoint : sessionAStreamEndpoint;
    }

    public static String configPath(String[] args) {
        if (args.length != 2 || !"--config".equals(args[0]) || args[1].isBlank()) {
            throw new IllegalArgumentException("Usage: <role executable> --config <path>");
        }
        return args[1];
    }

    private static String value(String value, String fallback) {
        return value == null || value.isBlank() ? fallback : value;
    }

    private static String required(String value, String name) {
        if (value == null || value.isBlank()) {
            throw new IllegalArgumentException("sample." + name + " is required");
        }
        return value;
    }
}
