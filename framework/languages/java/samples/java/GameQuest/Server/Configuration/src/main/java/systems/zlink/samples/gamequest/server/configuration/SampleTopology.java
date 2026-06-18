package systems.zlink.samples.gamequest.server.configuration;

import systems.zlink.contracts.core.RoutingId;

/**
 * Endpoint topology and {@code PlayerId} owner routing for GameQuest.
 *
 * <p>Like the other Java samples the run script reserves free TCP ports and
 * overrides the defaults below through {@code -Dzlink.samples.gamequest.*}
 * system properties. The .NET sample exposes the GameApi action surface over
 * HTTP; the Java sample mirrors the same scenario over ZLink client/server
 * channels, so the {@code *HttpBaseUrl} endpoints become channel endpoints.
 */
public final class SampleTopology {
    public static final String RegistryPubEndpoint =
        property("registryPubEndpoint", "tcp://127.0.0.1:47590");
    public static final String RegistryRouterEndpoint =
        property("registryRouterEndpoint", "tcp://127.0.0.1:47591");

    public static final String FanoutPublisherAEndpoint =
        property("fanoutPublisherAEndpoint", "tcp://127.0.0.1:47592");
    public static final String FanoutPublisherBEndpoint =
        property("fanoutPublisherBEndpoint", "tcp://127.0.0.1:47593");

    public static final String GameApiAActionEndpoint =
        property("gameApiAActionEndpoint", "tcp://127.0.0.1:47594");
    public static final String GameApiBActionEndpoint =
        property("gameApiBActionEndpoint", "tcp://127.0.0.1:47595");

    public static final String GameApiAStreamEndpoint =
        property("gameApiAStreamEndpoint", "tcp://127.0.0.1:47596");
    public static final String GameApiBStreamEndpoint =
        property("gameApiBStreamEndpoint", "tcp://127.0.0.1:47597");

    public static final String MissionAActionEndpoint =
        property("missionAActionEndpoint", "tcp://127.0.0.1:47598");
    public static final String MissionBActionEndpoint =
        property("missionBActionEndpoint", "tcp://127.0.0.1:47599");

    public static final String MissionASpotEndpoint =
        property("missionASpotEndpoint", "tcp://127.0.0.1:47600");
    public static final String MissionASpotRouterEndpoint =
        property("missionASpotRouterEndpoint", "tcp://127.0.0.1:47601");
    public static final String MissionBSpotEndpoint =
        property("missionBSpotEndpoint", "tcp://127.0.0.1:47602");
    public static final String MissionBSpotRouterEndpoint =
        property("missionBSpotRouterEndpoint", "tcp://127.0.0.1:47603");

    public static final RoutingId MissionASpotRid = RoutingId.from("7101");
    public static final RoutingId MissionBSpotRid = RoutingId.from("7102");

    public static final String StoreDirectory =
        property("storeDirectory", System.getProperty("java.io.tmpdir") + "/gamequest");

    private SampleTopology() {
    }

    public static String gameApiActionEndpoint(String apiName) {
        return "api-b".equals(apiName) ? GameApiBActionEndpoint : GameApiAActionEndpoint;
    }

    public static String gameApiStreamEndpoint(String apiName) {
        return "api-b".equals(apiName) ? GameApiBStreamEndpoint : GameApiAStreamEndpoint;
    }

    public static String fanoutPublisherEndpointForApi(String apiName) {
        return "api-b".equals(apiName) ? FanoutPublisherBEndpoint : FanoutPublisherAEndpoint;
    }

    public static QuestMissionInstanceTopology forQuestMission(String missionName) {
        return "mission-b".equals(missionName)
            ? new QuestMissionInstanceTopology("mission-b", MissionBSpotEndpoint, MissionBSpotRouterEndpoint, MissionBSpotRid, 1)
            : new QuestMissionInstanceTopology("mission-a", MissionASpotEndpoint, MissionASpotRouterEndpoint, MissionASpotRid, 0);
    }

    public static String missionActionEndpoint(String missionName) {
        return "mission-b".equals(missionName) ? MissionBActionEndpoint : MissionAActionEndpoint;
    }

    private static String property(String name, String defaultValue) {
        return System.getProperty("zlink.samples.gamequest." + name, defaultValue);
    }
}
