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
    string GameApiBStreamEndpoint)
{
    public static GameQuestTopology FromEnvironment()
    {
        return new GameQuestTopology(
            Required("GAMEQUEST_GAMEAPI_A_HTTP_BASE_URL"),
            Required("GAMEQUEST_GAMEAPI_B_HTTP_BASE_URL"),
            Required("GAMEQUEST_MISSION_A_HTTP_URL"),
            Required("GAMEQUEST_MISSION_B_HTTP_URL"),
            Required("GAMEQUEST_GAMEAPI_A_STREAM_ENDPOINT"),
            Required("GAMEQUEST_GAMEAPI_B_STREAM_ENDPOINT"));
    }

    private static string Required(string name)
    {
        return Environment.GetEnvironmentVariable(name)
               ?? throw new InvalidOperationException($"{name} is required.");
    }
}
