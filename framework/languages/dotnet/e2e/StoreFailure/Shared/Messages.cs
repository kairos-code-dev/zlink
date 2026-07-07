namespace StoreFailure.Shared;

public static class StoreFailureNames
{
    public const string Channel = "storefailure.profile";
}

public sealed record ProfileReq(string Value, string Marker);

public sealed record ProfileRes(string Value, string ProviderRid, string Marker);

public sealed record ProfileMsg(string Marker);

public sealed record EvidenceWaitReq(
    string[] ContainsAll,
    string[][] ContainsAnyGroups,
    int TimeoutMilliseconds = 10000);

public sealed record WeightWaitReq(
    int Expected,
    int TimeoutMilliseconds = 10000);

public sealed record StoreDelayReq(int Milliseconds);

public sealed record TopologyWaitReq(
    string RoutingId,
    string State,
    int ExpectedCount,
    int TimeoutMilliseconds = 30000);

public sealed record RegistryHealthWaitReq(
    bool ExpectedHealthy,
    int TimeoutMilliseconds = 30000);

public sealed record RuntimeStatusRes(
    bool StoreHealthy,
    bool WatchEnabled,
    string? LastError,
    bool OwnerLeaseHealthy,
    DateTimeOffset? OwnerLeaseRenewedAt,
    DateTimeOffset? LastRefreshAt);

public sealed record PeerRowRes(string? Rid, string Endpoint, string OwnerId);
