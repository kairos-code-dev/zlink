using GameQuest.Shared;
using Systems.Zlink;

namespace GameQuest.Server.Configuration;

public static class SampleNames
{
    public const string FanoutChannel = "gamequest.events";
    public const string GameplayTopic = "gameplay.event";
    public const string QuestSpotDiscovery = "gamequest.quest.spot";
    public const string QuestSpotNode = "gamequest.quest.node";
    public const string StreamNode = "gamequest.stream";
    public const string SubscribePacket = nameof(SubscribeQuestReq);
    public const string ProgressPacket = nameof(QuestProgressNotify);
    public const string CompletedPacket = nameof(QuestCompletedNotify);

    public static readonly TimeSpan RequestTimeout = TimeSpan.FromSeconds(10);
}

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
    string FanoutPublisherAEndpoint,
    string FanoutPublisherBEndpoint,
    string GameApiAHttpBaseUrl,
    string GameApiBHttpBaseUrl,
    string MissionAHttpBaseUrl,
    string MissionBHttpBaseUrl,
    string GameApiAStreamEndpoint,
    string GameApiBStreamEndpoint,
    string MissionASpotEndpoint,
    string MissionASpotRouterEndpoint,
    string MissionBSpotEndpoint,
    string MissionBSpotRouterEndpoint,
    string StoreDirectory,
    RoutingId MissionASpotRid,
    RoutingId MissionBSpotRid)
{
    public static GameQuestTopology FromEnvironment() => new(
        Required("GAMEQUEST_REDIS_ENDPOINT"),
        Environment.GetEnvironmentVariable("GAMEQUEST_REDIS_KEY_PREFIX") ?? "gamequest:",
        Required("GAMEQUEST_FANOUT_PUBLISHER_A_ENDPOINT"),
        Required("GAMEQUEST_FANOUT_PUBLISHER_B_ENDPOINT"),
        Required("GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL"),
        Required("GAMEQUEST_GAMEAPI_B_HTTP_BASE_URL"),
        Required("GAMEQUEST_MISSION_A_HTTP_URL"),
        Required("GAMEQUEST_MISSION_B_HTTP_URL"),
        Required("GAMEQUEST_GAMEAPI_A_STREAM_ENDPOINT"),
        Required("GAMEQUEST_GAMEAPI_B_STREAM_ENDPOINT"),
        Required("GAMEQUEST_MISSION_A_SPOT_ENDPOINT"),
        Required("GAMEQUEST_MISSION_A_SPOT_ROUTER_ENDPOINT"),
        Required("GAMEQUEST_MISSION_B_SPOT_ENDPOINT"),
        Required("GAMEQUEST_MISSION_B_SPOT_ROUTER_ENDPOINT"),
        Required("GAMEQUEST_STORE_DIR"),
        RoutingId.From("7101"),
        RoutingId.From("7102"));

    public QuestMissionInstanceTopology ForQuestMission(string missionName)
    {
        return string.Equals(missionName, "mission-b", StringComparison.Ordinal)
            ? new QuestMissionInstanceTopology(missionName, MissionBSpotEndpoint, MissionBSpotRouterEndpoint, MissionBSpotRid, OwnerIndex: 1)
            : new QuestMissionInstanceTopology("mission-a", MissionASpotEndpoint, MissionASpotRouterEndpoint, MissionASpotRid, OwnerIndex: 0);
    }

    private static string Required(string name) =>
        Environment.GetEnvironmentVariable(name)
        ?? throw new InvalidOperationException($"{name} is required.");

    public string FanoutPublisherEndpointForApi(string apiName) =>
        string.Equals(apiName, "api-b", StringComparison.Ordinal)
            ? FanoutPublisherBEndpoint
            : FanoutPublisherAEndpoint;
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
    RoutingId SpotRid,
    int OwnerIndex);
