package systems.zlink.samples.bingo.server.configuration;

public final class SampleTopology {
    public static final String RegistryPubEndpoint =
        property("registryPubEndpoint", "tcp://127.0.0.1:47101");
    public static final String RegistryRouterEndpoint =
        property("registryRouterEndpoint", "tcp://127.0.0.1:47102");
    public static final String ApiChannelEndpoint =
        property("apiChannelEndpoint", "tcp://127.0.0.1:47103");
    public static final String ApiAChannelEndpoint =
        property("apiAChannelEndpoint", ApiChannelEndpoint);
    public static final String ApiBChannelEndpoint =
        property("apiBChannelEndpoint", "tcp://127.0.0.1:47117");
    public static final String PlayChannelEndpoint =
        property("playChannelEndpoint", "tcp://127.0.0.1:47104");
    public static final String PlayAChannelEndpoint =
        property("playAChannelEndpoint", PlayChannelEndpoint);
    public static final String PlayBChannelEndpoint =
        property("playBChannelEndpoint", "tcp://127.0.0.1:47118");
    public static final String SessionSpotEndpoint =
        property("sessionSpotEndpoint", "tcp://127.0.0.1:47105");
    public static final String SessionRouterEndpoint =
        property("sessionRouterEndpoint", "tcp://127.0.0.1:47106");
    public static final String SessionASpotEndpoint =
        property("sessionASpotEndpoint", SessionSpotEndpoint);
    public static final String SessionBSpotEndpoint =
        property("sessionBSpotEndpoint", "tcp://127.0.0.1:47119");
    public static final String SessionARouterEndpoint =
        property("sessionARouterEndpoint", SessionRouterEndpoint);
    public static final String SessionBRouterEndpoint =
        property("sessionBRouterEndpoint", "tcp://127.0.0.1:47120");
    public static final String PlaySpotEndpoint =
        property("playSpotEndpoint", "tcp://127.0.0.1:47110");
    public static final String PlaySpotRouterEndpoint =
        property("playSpotRouterEndpoint", "tcp://127.0.0.1:47111");
    public static final String PlayASpotEndpoint =
        property("playASpotEndpoint", PlaySpotEndpoint);
    public static final String PlayBSpotEndpoint =
        property("playBSpotEndpoint", "tcp://127.0.0.1:47121");
    public static final String PlayASpotRouterEndpoint =
        property("playASpotRouterEndpoint", PlaySpotRouterEndpoint);
    public static final String PlayBSpotRouterEndpoint =
        property("playBSpotRouterEndpoint", "tcp://127.0.0.1:47122");
    public static final String SessionRouteEndpoint =
        property("sessionRouteEndpoint", "tcp://127.0.0.1:47112");
    public static final String PlayRouteEndpoint =
        property("playRouteEndpoint", "tcp://127.0.0.1:47113");
    public static final String SessionAPlayRouteEndpoint =
        property("sessionAPlayRouteEndpoint", SessionRouteEndpoint);
    public static final String SessionBPlayRouteEndpoint =
        property("sessionBPlayRouteEndpoint", "tcp://127.0.0.1:47123");
    public static final String PlayARouteEndpoint =
        property("playARouteEndpoint", PlayRouteEndpoint);
    public static final String PlayBRouteEndpoint =
        property("playBRouteEndpoint", "tcp://127.0.0.1:47124");
    public static final String StreamEndpoint =
        property("streamEndpoint", "tcp://127.0.0.1:47114");
    public static final String SessionAStreamEndpoint =
        property("sessionAStreamEndpoint", StreamEndpoint);
    public static final String SessionBStreamEndpoint =
        property("sessionBStreamEndpoint", "tcp://127.0.0.1:47125");
    public static final String RedisEndpoint =
        property("redisEndpoint", "127.0.0.1:6379");
    public static final String RedisKeyPrefix =
        property("redisKeyPrefix", "bingo:java:");
    public static final String ApiNode = property("apiNode", "a");
    public static final String PlayNode = property("playNode", "a");
    public static final String SessionNode = property("sessionNode", "a");
    public static final String SessionARouteRid = property("sessionAPlayRouteRid", "1201");
    public static final String SessionBRouteRid = property("sessionBPlayRouteRid", "1202");
    public static final String ApiARouteRid = property("apiAPlayRouteRid", "1301");
    public static final String ApiBRouteRid = property("apiBPlayRouteRid", "1302");
    public static final String SessionARouterRid = property("sessionARouterRid", "1101");
    public static final String SessionBRouterRid = property("sessionBRouterRid", "1102");
    public static final String PlayANodeRid = property("playANodeRid", "2201");
    public static final String PlayBNodeRid = property("playBNodeRid", "2202");
    public static final String PlayRid = property("playRid", PlayBNodeRid);

    private SampleTopology() {
    }

    public static String selectedApiChannelEndpoint() {
        return "b".equals(ApiNode) ? ApiBChannelEndpoint : ApiAChannelEndpoint;
    }

    public static String selectedApiRouteRid() {
        return "b".equals(ApiNode) ? ApiBRouteRid : ApiARouteRid;
    }

    public static String selectedPlayChannelEndpoint() {
        return "b".equals(PlayNode) ? PlayBChannelEndpoint : PlayAChannelEndpoint;
    }

    public static String selectedPlayRouteEndpoint() {
        return "b".equals(PlayNode) ? PlayBRouteEndpoint : PlayARouteEndpoint;
    }

    public static String peerPlayRouteEndpoint() {
        return "b".equals(PlayNode) ? PlayARouteEndpoint : PlayBRouteEndpoint;
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

    public static String selectedSessionRouteEndpoint() {
        return "b".equals(SessionNode) ? SessionBPlayRouteEndpoint : SessionAPlayRouteEndpoint;
    }

    public static String selectedSessionRouteRid() {
        return "b".equals(SessionNode) ? SessionBRouteRid : SessionARouteRid;
    }

    public static String selectedStreamEndpoint() {
        return "b".equals(SessionNode) ? SessionBStreamEndpoint : SessionAStreamEndpoint;
    }

    private static String property(String name, String defaultValue) {
        return System.getProperty("zlink.samples.bingo." + name, defaultValue);
    }
}
