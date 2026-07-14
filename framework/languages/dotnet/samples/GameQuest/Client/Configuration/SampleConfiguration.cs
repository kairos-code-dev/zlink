namespace GameQuest.Client.Configuration;

public static class SampleNames
{
    public static readonly TimeSpan RequestTimeout = TimeSpan.FromSeconds(10);
}

public static class QuestIds
{
    public const string FirstHunt = "first-hunt";
    public const string OpenAuction = "open-auction";
    public const string HerbGathering = "herb-gathering";
}

public static class QuestStatuses
{
    public const string RewardGranted = "RewardGranted";
}

public sealed record GameQuestTopology(
    string GameApiAHttpBaseUrl,
    string GameApiBHttpBaseUrl,
    string MissionAHttpBaseUrl,
    string MissionBHttpBaseUrl,
    string GameApiAStreamEndpoint,
    string GameApiBStreamEndpoint);
