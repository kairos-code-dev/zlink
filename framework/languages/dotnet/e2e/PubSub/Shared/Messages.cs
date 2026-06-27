namespace PubSub.Shared;

public static class PubSubNames
{
    public const string Channel = "events";
    public const string MainTopic = "orders";
    public const string OtherTopic = "billing";
}

public sealed record EventNotify(string RunId, int Sequence, string Value);

public sealed record MissingEventNotify(string RunId, int Sequence, string Value);

public sealed record EvidenceWaitRequest(
    string[] ContainsAll,
    string[][] ContainsAnyGroups,
    int TimeoutMilliseconds = 10000)
{
    public string[][] ContainsAllLineGroups { get; init; } = [];
    public string[][] ContainsAnyLineGroups { get; init; } = [];
}
