using GameQuest.Shared;
using Microsoft.Extensions.Configuration;
using Systems.Zlink;

namespace GameQuest.Server.Configuration;

public static class SampleNames
{
    public const string GameApiChannel = "gamequest.session.api";
    public const string SessionSpotDiscovery = "gamequest.session.spot";
    public const string SessionActorType = "gamequest.session.actor";
    public const string QuestOwnerRouteChannel = "gamequest.quest.owner";
    public const string QuestSpotDiscovery = "gamequest.quest.spot";
    public const string QuestSpotNode = "gamequest.quest.node";
    public const string StreamNode = "gamequest.stream";
    public const string ProgressPacket = nameof(QuestProgressNotify);
    public const string CompletedPacket = nameof(QuestCompletedNotify);

    public static readonly TimeSpan RequestTimeout = TimeSpan.FromSeconds(10);

    public static string QuestOwnerChannelFor(string missionName) => $"gamequest.quest.owner.{missionName}";

    public static string GameApiChannelFor(string apiName) => $"gamequest.game-api.{apiName}";
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
    string GameApiAHttpBaseUrl,
    string GameApiBHttpBaseUrl,
    string MissionAHttpBaseUrl,
    string MissionBHttpBaseUrl,
    string GameApiAStreamEndpoint,
    string GameApiBStreamEndpoint,
    string GameApiAChannelEndpoint,
    string GameApiBChannelEndpoint,
    string MissionAChannelEndpoint,
    string MissionBChannelEndpoint,
    string MissionASpotEndpoint,
    string MissionASpotRouterEndpoint,
    string MissionBSpotEndpoint,
    string MissionBSpotRouterEndpoint,
    string GameApiASpotEndpoint,
    string GameApiASpotRouterEndpoint,
    string GameApiBSpotEndpoint,
    string GameApiBSpotRouterEndpoint,
    RoutingId MissionASpotRid,
    RoutingId MissionBSpotRid,
    RoutingId GameApiARouteRid,
    RoutingId GameApiBRouteRid,
    RoutingId GameApiASpotRid,
    RoutingId GameApiBSpotRid)
{
    public static GameQuestRuntimeConfiguration LoadGameApi(string[] args) => Load(args, "api");

    public static GameQuestRuntimeConfiguration LoadQuestMission(string[] args) => Load(args, "mission");

    private static GameQuestRuntimeConfiguration Load(string[] args, string role)
    {
        if (args.Length != 2 || args[0] != "--config")
            throw new ArgumentException("Usage: --config PATH");
        var settings = new ConfigurationBuilder()
                           .AddJsonFile(Path.GetFullPath(args[1]), optional: false, reloadOnChange: false)
                           .Build()
                           .GetRequiredSection("Sample")
                           .Get<GameQuestConfiguration>()
                       ?? throw new InvalidOperationException("GameQuest Sample configuration is empty.");
        settings.Validate(role);
        var topology = new GameQuestTopology(
            settings.RedisEndpoint,
            settings.RedisKeyPrefix,
            settings.GameApiAHttpBaseUrl,
            settings.GameApiBHttpBaseUrl,
            settings.MissionAHttpBaseUrl,
            settings.MissionBHttpBaseUrl,
            settings.GameApiAStreamEndpoint,
            settings.GameApiBStreamEndpoint,
            settings.GameApiAChannelEndpoint,
            settings.GameApiBChannelEndpoint,
            settings.MissionAChannelEndpoint,
            settings.MissionBChannelEndpoint,
            settings.MissionASpotEndpoint,
            settings.MissionASpotRouterEndpoint,
            settings.MissionBSpotEndpoint,
            settings.MissionBSpotRouterEndpoint,
            settings.GameApiASpotEndpoint,
            settings.GameApiASpotRouterEndpoint,
            settings.GameApiBSpotEndpoint,
            settings.GameApiBSpotRouterEndpoint,
            RoutingId.From("7101"),
            RoutingId.From("7102"),
            RoutingId.From("7001"),
            RoutingId.From("7002"),
            RoutingId.From("7201"),
            RoutingId.From("7202"));
        var streamBindEndpoint = string.Equals(settings.InstanceName, "api-b", StringComparison.Ordinal)
            ? settings.GameApiBStreamBindEndpoint
            : settings.GameApiAStreamBindEndpoint;
        return new GameQuestRuntimeConfiguration(
            topology,
            settings.InstanceName,
            settings.LogDirectory,
            streamBindEndpoint);
    }

    public QuestMissionInstanceTopology ForQuestMission(string missionName)
    {
        return string.Equals(missionName, "mission-b", StringComparison.Ordinal)
            ? new QuestMissionInstanceTopology(missionName, MissionBSpotEndpoint, MissionBSpotRouterEndpoint, MissionBSpotRid, OwnerIndex: 1)
            : new QuestMissionInstanceTopology("mission-a", MissionASpotEndpoint, MissionASpotRouterEndpoint, MissionASpotRid, OwnerIndex: 0);
    }

    public string MissionHttpBaseUrl(string missionName) =>
        string.Equals(missionName, "mission-b", StringComparison.Ordinal)
            ? MissionBHttpBaseUrl
            : MissionAHttpBaseUrl;

    public RoutingId RouteRidForApi(string apiName) =>
        string.Equals(apiName, "api-b", StringComparison.Ordinal)
            ? GameApiBRouteRid
            : GameApiARouteRid;

    public RoutingId OwnerRouteRid(string playerId) =>
        GameQuestRouting.OwnerIndex(playerId) == 1 ? MissionBSpotRid : MissionASpotRid;

    public string QuestOwnerChannel(string playerId) =>
        SampleNames.QuestOwnerChannelFor(GameQuestRouting.OwnerIndex(playerId) == 1 ? "mission-b" : "mission-a");

    public string GameApiChannelEndpoint(string apiName) =>
        string.Equals(apiName, "api-b", StringComparison.Ordinal)
            ? GameApiBChannelEndpoint
            : GameApiAChannelEndpoint;

    public string GameApiSpotEndpoint(string apiName) =>
        string.Equals(apiName, "api-b", StringComparison.Ordinal)
            ? GameApiBSpotEndpoint
            : GameApiASpotEndpoint;

    public string GameApiSpotRouterEndpoint(string apiName) =>
        string.Equals(apiName, "api-b", StringComparison.Ordinal)
            ? GameApiBSpotRouterEndpoint
            : GameApiASpotRouterEndpoint;

    public RoutingId GameApiSpotRid(string apiName) =>
        string.Equals(apiName, "api-b", StringComparison.Ordinal)
            ? GameApiBSpotRid
            : GameApiASpotRid;

    public string MissionChannelEndpoint(string missionName) =>
        string.Equals(missionName, "mission-b", StringComparison.Ordinal)
            ? MissionBChannelEndpoint
            : MissionAChannelEndpoint;
}

public sealed record GameQuestRuntimeConfiguration(
    GameQuestTopology Topology,
    string InstanceName,
    string LogDirectory,
    string StreamBindEndpoint);

public sealed class GameQuestConfiguration
{
    public string InstanceName { get; init; } = "";
    public string LogDirectory { get; init; } = "";
    public string RedisEndpoint { get; init; } = "";
    public string RedisKeyPrefix { get; init; } = "";
    public string GameApiAHttpBaseUrl { get; init; } = "";
    public string GameApiBHttpBaseUrl { get; init; } = "";
    public string MissionAHttpBaseUrl { get; init; } = "";
    public string MissionBHttpBaseUrl { get; init; } = "";
    public string GameApiAStreamEndpoint { get; init; } = "";
    public string GameApiBStreamEndpoint { get; init; } = "";
    public string GameApiAStreamBindEndpoint { get; init; } = "";
    public string GameApiBStreamBindEndpoint { get; init; } = "";
    public string GameApiAChannelEndpoint { get; init; } = "";
    public string GameApiBChannelEndpoint { get; init; } = "";
    public string MissionAChannelEndpoint { get; init; } = "";
    public string MissionBChannelEndpoint { get; init; } = "";
    public string MissionASpotEndpoint { get; init; } = "";
    public string MissionASpotRouterEndpoint { get; init; } = "";
    public string MissionBSpotEndpoint { get; init; } = "";
    public string MissionBSpotRouterEndpoint { get; init; } = "";
    public string GameApiASpotEndpoint { get; init; } = "";
    public string GameApiASpotRouterEndpoint { get; init; } = "";
    public string GameApiBSpotEndpoint { get; init; } = "";
    public string GameApiBSpotRouterEndpoint { get; init; } = "";

    public void Validate(string role)
    {
        Require(InstanceName, nameof(InstanceName));
        Require(LogDirectory, nameof(LogDirectory));
        Require(RedisEndpoint, nameof(RedisEndpoint));
        Require(RedisKeyPrefix, nameof(RedisKeyPrefix));
        var isB = InstanceName.EndsWith("-b", StringComparison.Ordinal);
        if (role == "api")
        {
            Require(isB ? GameApiBHttpBaseUrl : GameApiAHttpBaseUrl,
                isB ? nameof(GameApiBHttpBaseUrl) : nameof(GameApiAHttpBaseUrl));
            Require(isB ? GameApiBStreamBindEndpoint : GameApiAStreamBindEndpoint,
                isB ? nameof(GameApiBStreamBindEndpoint) : nameof(GameApiAStreamBindEndpoint));
            Require(isB ? GameApiBChannelEndpoint : GameApiAChannelEndpoint,
                isB ? nameof(GameApiBChannelEndpoint) : nameof(GameApiAChannelEndpoint));
            Require(isB ? GameApiBSpotEndpoint : GameApiASpotEndpoint,
                isB ? nameof(GameApiBSpotEndpoint) : nameof(GameApiASpotEndpoint));
            Require(isB ? GameApiBSpotRouterEndpoint : GameApiASpotRouterEndpoint,
                isB ? nameof(GameApiBSpotRouterEndpoint) : nameof(GameApiASpotRouterEndpoint));
            return;
        }
        if (role != "mission")
            throw new InvalidOperationException($"Unknown GameQuest role '{role}'.");
        Require(GameApiAHttpBaseUrl, nameof(GameApiAHttpBaseUrl));
        Require(isB ? MissionBHttpBaseUrl : MissionAHttpBaseUrl,
            isB ? nameof(MissionBHttpBaseUrl) : nameof(MissionAHttpBaseUrl));
        Require(isB ? MissionBChannelEndpoint : MissionAChannelEndpoint,
            isB ? nameof(MissionBChannelEndpoint) : nameof(MissionAChannelEndpoint));
        Require(isB ? MissionBSpotEndpoint : MissionASpotEndpoint,
            isB ? nameof(MissionBSpotEndpoint) : nameof(MissionASpotEndpoint));
        Require(isB ? MissionBSpotRouterEndpoint : MissionASpotRouterEndpoint,
            isB ? nameof(MissionBSpotRouterEndpoint) : nameof(MissionASpotRouterEndpoint));
    }

    private static void Require(string value, string name)
    {
        if (string.IsNullOrWhiteSpace(value))
            throw new InvalidOperationException($"GameQuest Sample.{name} is required.");
    }
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
