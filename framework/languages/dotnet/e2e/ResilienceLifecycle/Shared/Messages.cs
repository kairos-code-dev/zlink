namespace ResilienceLifecycle.Shared;

public static class ResilienceLifecycleNames
{
    public const string Channel = "resilience.profile";
}

public sealed record ProfileReq(string Value, string Marker);

public sealed record MissingProfileReq(string Value, string Marker);

public sealed record ProfileRes(string Value, string ProviderRid, string Marker);

public sealed record ProfileAttemptRes(
    ProfileRes? Reply,
    string? ErrorKind,
    bool IsRetriable);

public sealed record ProfileMsg(string Marker);

public sealed record EvidenceWaitReq(
    string[] ContainsAll,
    string[][] ContainsAnyGroups,
    int TimeoutMilliseconds = 10000);

public sealed record WeightWaitReq(
    int Expected,
    int TimeoutMilliseconds = 10000);

public sealed record TopologyWaitReq(
    string RoutingId,
    string State,
    int ExpectedCount,
    int TimeoutMilliseconds = 30000,
    uint? ExpectedWeight = null);

public sealed record RegistryHealthWaitReq(
    bool ExpectedHealthy,
    int TimeoutMilliseconds = 30000);
