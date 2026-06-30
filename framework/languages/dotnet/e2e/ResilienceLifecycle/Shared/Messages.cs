namespace ResilienceLifecycle.Shared;

public static class ResilienceLifecycleNames
{
    public const string Channel = "resilience.profile";
}

public sealed record ProfileRequest(string Value, string Marker);

public sealed record ProfileReply(string Value, string ProviderRid, string Marker);

public sealed record ProfileCommand(string Marker);

public sealed record EvidenceWaitRequest(
    string[] ContainsAll,
    string[][] ContainsAnyGroups,
    int TimeoutMilliseconds = 10000);

public sealed record WeightWaitRequest(
    int Expected,
    int TimeoutMilliseconds = 10000);

public sealed record TopologyWaitRequest(
    string RoutingId,
    string State,
    int ExpectedCount,
    int TimeoutMilliseconds = 30000);

public sealed record RegistryHealthWaitRequest(
    bool ExpectedHealthy,
    int TimeoutMilliseconds = 30000);