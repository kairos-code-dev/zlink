namespace Zlink.Framework.Contracts.Locations;

public sealed record ZLinkLocationRuntimeStatus(
    bool StoreHealthy,
    bool WatchEnabled,
    TimeSpan PollingInterval,
    DateTimeOffset? LastRefreshAt,
    string? LastError,
    bool OwnerLeaseHealthy,
    DateTimeOffset? OwnerLeaseRenewedAt);

public enum ZLinkLocationTopologyState
{
    Discovered = 1,
    Connecting = 2,
    Ready = 3,
    Lost = 4,
    Error = 5,
    Stopped = 6
}

public sealed record ZLinkLocationTopologyFilter(
    ZLinkLocationKind? Kind = null,
    string? MeshName = null,
    ZLinkLocationRole? Role = null,
    RoutingId? NodeRid = null,
    ZLinkLocationTopologyState? State = null);

public sealed record ZLinkLocationTopologyEntry(
    ZLinkLocationKind Kind,
    string? MeshName,
    ZLinkLocationRole? Role,
    RoutingId? NodeRid,
    RoutingId? SpotRid,
    string? ActorId,
    string? Endpoint,
    ZLinkLocationTopologyState State,
    uint DesiredCount,
    uint ReadyCount,
    int ErrorCode,
    DateTimeOffset UpdatedAt);

public sealed record ZLinkLocationServiceSummaryFilter(
    string? MeshName = null,
    ZLinkLocationAutoConnectType? AutoConnectType = null,
    ZLinkLocationRole? Role = null);

public sealed record ZLinkLocationServiceSummary(
    string MeshName,
    ZLinkLocationAutoConnectType AutoConnectType,
    ZLinkLocationRole Role,
    uint TotalCount,
    uint ReadyCount,
    uint ErrorCount,
    uint StoppedCount,
    DateTimeOffset LastUpdatedAt);
