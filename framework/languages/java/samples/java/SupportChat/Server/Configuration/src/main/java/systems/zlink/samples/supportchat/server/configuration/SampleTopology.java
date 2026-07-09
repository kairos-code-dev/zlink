package systems.zlink.samples.supportchat.server.configuration;

public final class SampleTopology {
    public static final String ApiChannelEndpoint =
        env("SUPPORTCHAT_API_CHANNEL_ENDPOINT", "tcp://127.0.0.1:19611");
    public static final String SupportChannelEndpoint =
        env("SUPPORTCHAT_SUPPORT_CHANNEL_ENDPOINT", "tcp://127.0.0.1:19612");
    public static final String SessionStreamEndpoint =
        env("SUPPORTCHAT_SESSION_STREAM_ENDPOINT", "tcp://127.0.0.1:19613");
    public static final String SessionSpotRouterEndpoint =
        env("SUPPORTCHAT_SESSION_SPOT_ROUTER_ENDPOINT", "tcp://127.0.0.1:19614");
    public static final String SupportSpotRouterEndpoint =
        env("SUPPORTCHAT_SUPPORT_SPOT_ROUTER_ENDPOINT", "tcp://127.0.0.1:19615");
    public static final String SessionSpotNodeRid =
        env("SUPPORTCHAT_SESSION_SPOT_NODE_RID", "supportchat-session-node");
    public static final String SupportSpotNodeRid =
        env("SUPPORTCHAT_SUPPORT_SPOT_NODE_RID", "supportchat-support-node");
    public static final String ApiHttpEndpoint =
        env("SUPPORTCHAT_API_HTTP_ENDPOINT", "http://127.0.0.1:19621");
    public static final String SupportHttpEndpoint =
        env("SUPPORTCHAT_SUPPORT_HTTP_ENDPOINT", "http://127.0.0.1:19622");
    public static final String RedisEndpoint =
        env("SUPPORTCHAT_REDIS_ENDPOINT", "127.0.0.1:6379");
    public static final String RedisKeyPrefix =
        env("SUPPORTCHAT_REDIS_KEY_PREFIX", "zlink:supportchat:sample");

    private SampleTopology() {
    }

    private static String env(String name, String fallback) {
        String value = System.getenv(name);
        return value == null || value.isBlank() ? fallback : value;
    }
}
