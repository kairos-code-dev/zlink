package systems.zlink.samples.gamequest.server.configuration;

import java.nio.charset.StandardCharsets;
import systems.zlink.contracts.core.RoutingId;

public final class SampleTopology {
    public static final String ApiAStreamEndpoint = property(
        "zlink.samples.gamequest.apiAStreamEndpoint",
        "tcp://127.0.0.1:28301");
    public static final String ApiBStreamEndpoint = property(
        "zlink.samples.gamequest.apiBStreamEndpoint",
        "tcp://127.0.0.1:28302");
    public static final String ApiAHttpEndpoint = property(
        "zlink.samples.gamequest.apiAHttpEndpoint",
        "http://127.0.0.1:28311");
    public static final String ApiBHttpEndpoint = property(
        "zlink.samples.gamequest.apiBHttpEndpoint",
        "http://127.0.0.1:28312");
    public static final String MissionARouteEndpoint = property(
        "zlink.samples.gamequest.missionARouteEndpoint",
        "tcp://127.0.0.1:28321");
    public static final String MissionBRouteEndpoint = property(
        "zlink.samples.gamequest.missionBRouteEndpoint",
        "tcp://127.0.0.1:28322");
    public static final String MissionAHttpEndpoint = property(
        "zlink.samples.gamequest.missionAHttpEndpoint",
        "http://127.0.0.1:28331");
    public static final String MissionBHttpEndpoint = property(
        "zlink.samples.gamequest.missionBHttpEndpoint",
        "http://127.0.0.1:28332");
    public static final String RedisEndpoint = property(
        "zlink.samples.gamequest.redisEndpoint",
        "127.0.0.1:6379");
    public static final String RedisKeyPrefix = property(
        "zlink.samples.gamequest.redisKeyPrefix",
        "gamequest:java:");

    private SampleTopology() {
    }

    public static String apiName() {
        return System.getProperty("zlink.samples.gamequest.apiName", "api-a");
    }

    public static String missionName() {
        return System.getProperty("zlink.samples.gamequest.missionName", "mission-a");
    }

    public static String selectedApiStreamEndpoint() {
        return "api-b".equals(apiName()) ? ApiBStreamEndpoint : ApiAStreamEndpoint;
    }

    public static String selectedApiHttpEndpoint() {
        return "api-b".equals(apiName()) ? ApiBHttpEndpoint : ApiAHttpEndpoint;
    }

    public static String selectedMissionRouteEndpoint() {
        return "mission-b".equals(missionName()) ? MissionBRouteEndpoint : MissionARouteEndpoint;
    }

    public static String selectedMissionHttpEndpoint() {
        return "mission-b".equals(missionName()) ? MissionBHttpEndpoint : MissionAHttpEndpoint;
    }

    public static String selectedMissionRid() {
        return "mission-b".equals(missionName()) ? "gamequest-mission-b" : "gamequest-mission-a";
    }

    public static RoutingId ownerRouteRid(String playerId) {
        return RoutingId.from(ownerIndex(playerId) == 1 ? "gamequest-mission-b" : "gamequest-mission-a");
    }

    private static int ownerIndex(String playerId) {
        int sum = 0;
        for (byte value : playerId.getBytes(StandardCharsets.UTF_8)) {
            sum += Byte.toUnsignedInt(value);
        }
        return sum % 2;
    }

    private static String property(String name, String fallback) {
        return System.getProperty(name, fallback);
    }
}
