using GameQuest.Shared;
using Systems.Zlink;

namespace GameQuest.Server.Configuration;

public static class SampleNames
{
    public const string QuestOwnerRouteChannel = "gamequest.quest.owner";
    public const string QuestSpotDiscovery = "gamequest.quest.spot";
    public const string QuestSpotNode = "gamequest.quest.node";
    public const string StreamNode = "gamequest.stream";
    public const string ProgressPacket = nameof(QuestProgressNotify);
    public const string CompletedPacket = nameof(QuestCompletedNotify);

    public static readonly TimeSpan RequestTimeout = TimeSpan.FromSeconds(10);
}

public sealed record ApplyGameplayEventReq(GameplayEventEnvelope Event);

public sealed record ApplyGameplayEventRes(bool Applied);

public static class QuestIds
{
    public const string FirstHunt = "first-hunt";
    public const string OpenAuction = "open-auction";
    public const string HerbGathering = "herb-gathering";
    public const string ClearTutorial = "clear-tutorial";
    public const string VisitRuins = "visit-ruins";
}

public static class QuestStatuses
{
    public const string Active = "Active";
    public const string Completed = "Completed";
    public const string RewardGranted = "RewardGranted";
}

public sealed record GameQuestTopology(
    string RedisEndpoint,
    string RedisKeyPrefix,
    string GameApiARouteEndpoint,
    string GameApiBRouteEndpoint,
    string GameApiAHttpBaseUrl,
    string GameApiBHttpBaseUrl,
    string MissionAHttpBaseUrl,
    string MissionBHttpBaseUrl,
    string GameApiAStreamEndpoint,
    string GameApiBStreamEndpoint,
    string MissionASpotEndpoint,
    string MissionASpotRouterEndpoint,
    string MissionARouteEndpoint,
    string MissionBSpotEndpoint,
    string MissionBSpotRouterEndpoint,
    string MissionBRouteEndpoint,
    RoutingId MissionASpotRid,
    RoutingId MissionBSpotRid,
    RoutingId GameApiARouteRid,
    RoutingId GameApiBRouteRid)
{
    public static GameQuestTopology FromEnvironment() => new(
        Required("GAMEQUEST_REDIS_ENDPOINT"),
        Environment.GetEnvironmentVariable("GAMEQUEST_REDIS_KEY_PREFIX") ?? "gamequest:",
        Required("GAMEQUEST_GAMEAPI_A_ROUTE_ENDPOINT"),
        Required("GAMEQUEST_GAMEAPI_B_ROUTE_ENDPOINT"),
        Required("GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL"),
        Required("GAMEQUEST_GAMEAPI_B_HTTP_BASE_URL"),
        Required("GAMEQUEST_MISSION_A_HTTP_URL"),
        Required("GAMEQUEST_MISSION_B_HTTP_URL"),
        Required("GAMEQUEST_GAMEAPI_A_STREAM_ENDPOINT"),
        Required("GAMEQUEST_GAMEAPI_B_STREAM_ENDPOINT"),
        Required("GAMEQUEST_MISSION_A_SPOT_ENDPOINT"),
        Required("GAMEQUEST_MISSION_A_SPOT_ROUTER_ENDPOINT"),
        Required("GAMEQUEST_MISSION_A_ROUTE_ENDPOINT"),
        Required("GAMEQUEST_MISSION_B_SPOT_ENDPOINT"),
        Required("GAMEQUEST_MISSION_B_SPOT_ROUTER_ENDPOINT"),
        Required("GAMEQUEST_MISSION_B_ROUTE_ENDPOINT"),
        RoutingId.From("7101"),
        RoutingId.From("7102"),
        RoutingId.From("7001"),
        RoutingId.From("7002"));

    public QuestMissionInstanceTopology ForQuestMission(string missionName)
    {
        return string.Equals(missionName, "mission-b", StringComparison.Ordinal)
            ? new QuestMissionInstanceTopology(missionName, MissionBSpotEndpoint, MissionBSpotRouterEndpoint, MissionBRouteEndpoint, MissionBSpotRid, OwnerIndex: 1)
            : new QuestMissionInstanceTopology("mission-a", MissionASpotEndpoint, MissionASpotRouterEndpoint, MissionARouteEndpoint, MissionASpotRid, OwnerIndex: 0);
    }

    private static string Required(string name) =>
        Environment.GetEnvironmentVariable(name)
        ?? throw new InvalidOperationException($"{name} is required.");

    public string RouteEndpointForApi(string apiName) =>
        string.Equals(apiName, "api-b", StringComparison.Ordinal)
            ? GameApiBRouteEndpoint
            : GameApiARouteEndpoint;

    public RoutingId RouteRidForApi(string apiName) =>
        string.Equals(apiName, "api-b", StringComparison.Ordinal)
            ? GameApiBRouteRid
            : GameApiARouteRid;

    public RoutingId OwnerRouteRid(string playerId) =>
        GameQuestRouting.OwnerIndex(playerId) == 1 ? MissionBSpotRid : MissionASpotRid;
}

public static class GameQuestRouting
{
    public static int OwnerIndex(string playerId)
    {
        var sum = 0;
        foreach (var value in System.Text.Encoding.UTF8.GetBytes(playerId))
        {
            sum += value;
        }

        return sum % 2;
    }
}

public sealed record QuestMissionInstanceTopology(
    string MissionName,
    string SpotEndpoint,
    string SpotRouterEndpoint,
    string RouteEndpoint,
    RoutingId SpotRid,
    int OwnerIndex);
