using Zlink.Framework.Contracts.Spots;

namespace Zlink.Framework.Contracts.Locations;

public enum ZLinkLocationAutoConnectType
{
    Invalid = 0,
    RouteMesh = 1,
    ClientServer = 2,
    DealerMesh = 3,
    Fanout = 4,
    SpotMesh = 5
}

public enum ZLinkLocationRole : ushort
{
    Invalid = 0,
    Spot = 2,
    Router = 3,
    Dealer = 4,
    Pub = 5,
    Sub = 6
}

public enum ZLinkRouteKind
{
    Invalid = 0,
    ActorSession = 1,
    SpotName = 2,
    FrameworkRoute = 3
}

public enum ZLinkLocationKind
{
    Peer = 1,
    Spot = 2,
    Actor = 3,
    Route = 4
}

/// <summary>
/// Maps the closed value sets to the canonical lower-case strings used in
/// store keys. Every language implementation must store the same strings so
/// that different runtimes sharing one store can find each other's rows.
/// </summary>
public static class ZLinkLocationCanonicalNames
{
    public static string ToCanonicalString(this ZLinkLocationAutoConnectType type) => type switch
    {
        ZLinkLocationAutoConnectType.RouteMesh => "route-mesh",
        ZLinkLocationAutoConnectType.ClientServer => "client-server",
        ZLinkLocationAutoConnectType.DealerMesh => "dealer-mesh",
        ZLinkLocationAutoConnectType.Fanout => "fanout",
        ZLinkLocationAutoConnectType.SpotMesh => "spot-mesh",
        _ => throw new ArgumentOutOfRangeException(nameof(type), type, "Unknown auto-connect type.")
    };

    public static string ToCanonicalString(this ZLinkLocationRole role) => role switch
    {
        ZLinkLocationRole.Router => "router",
        ZLinkLocationRole.Dealer => "dealer",
        ZLinkLocationRole.Pub => "pub",
        ZLinkLocationRole.Sub => "sub",
        ZLinkLocationRole.Spot => "spot",
        _ => throw new ArgumentOutOfRangeException(nameof(role), role, "Unknown location role.")
    };

    public static bool TryParseAutoConnectType(string value, out ZLinkLocationAutoConnectType type)
    {
        type = value switch
        {
            "route-mesh" => ZLinkLocationAutoConnectType.RouteMesh,
            "client-server" => ZLinkLocationAutoConnectType.ClientServer,
            "dealer-mesh" => ZLinkLocationAutoConnectType.DealerMesh,
            "fanout" => ZLinkLocationAutoConnectType.Fanout,
            "spot-mesh" => ZLinkLocationAutoConnectType.SpotMesh,
            _ => ZLinkLocationAutoConnectType.Invalid
        };
        return type != ZLinkLocationAutoConnectType.Invalid;
    }

    public static bool TryParseRole(string value, out ZLinkLocationRole role)
    {
        role = value switch
        {
            "router" => ZLinkLocationRole.Router,
            "dealer" => ZLinkLocationRole.Dealer,
            "pub" => ZLinkLocationRole.Pub,
            "sub" => ZLinkLocationRole.Sub,
            "spot" => ZLinkLocationRole.Spot,
            _ => ZLinkLocationRole.Invalid
        };
        return role != ZLinkLocationRole.Invalid;
    }

    /// <summary>The closed value sets of draft 6.5: rows read from a store
    /// with values outside them are ignored by reconcile/resolve, and the
    /// registration path rejects them as a validation error.</summary>
    internal static bool IsKnown(ZLinkLocationAutoConnectType type) => type is
        ZLinkLocationAutoConnectType.RouteMesh
        or ZLinkLocationAutoConnectType.ClientServer
        or ZLinkLocationAutoConnectType.DealerMesh
        or ZLinkLocationAutoConnectType.Fanout
        or ZLinkLocationAutoConnectType.SpotMesh;

    internal static bool IsKnown(ZLinkLocationRole role) => role is
        ZLinkLocationRole.Spot
        or ZLinkLocationRole.Router
        or ZLinkLocationRole.Dealer
        or ZLinkLocationRole.Pub
        or ZLinkLocationRole.Sub;
}

public sealed record ZLinkPeerLocation(
    ZLinkLocationAutoConnectType AutoConnectType,
    string MeshName,
    RoutingId? NodeRid,
    ZLinkLocationRole Role,
    string Endpoint,
    uint Weight,
    long Value,
    IReadOnlyDictionary<string, string>? Metadata,
    IReadOnlyList<string>? Capabilities,
    string OwnerId,
    long Generation,
    DateTimeOffset UpdatedAt);

public sealed record ZLinkSpotLocation(
    string MeshName,
    RoutingId SpotRid,
    string? SpotType,
    RoutingId NodeRid,
    ZLinkSpotKind SpotKind,
    string? RouteEndpoint,
    string OwnerId,
    long Generation,
    DateTimeOffset UpdatedAt);

public sealed record ZLinkActorLocation(
    string ActorType,
    string ActorId,
    string ActorRef,
    RoutingId NodeRid,
    long Generation,
    ZLinkSpotKind LocationKind,
    RoutingId? SpotRid,
    ZLinkSpotKind SpotKind,
    string OwnerId,
    DateTimeOffset UpdatedAt)
{
    public string SpotMeshName { get; init; } = string.Empty;
}

public sealed record ZLinkRouteLocation(
    ZLinkRouteKind RouteKind,
    string RouteKey,
    RoutingId OwnerNodeRid,
    string OwnerId,
    long Generation,
    ReadOnlyMemory<byte> Value,
    DateTimeOffset UpdatedAt);

/// <summary>
/// One lease per framework runtime instance. Location rows do not carry a
/// lease of their own; a row is alive only while its owner's lease is alive.
/// </summary>
public sealed record ZLinkOwnerLease(
    string OwnerId,
    RoutingId NodeRid,
    DateTimeOffset LeaseExpiresAt,
    DateTimeOffset UpdatedAt);

/// <summary>
/// Owner lease list plus the store's current time. Expiry is judged from
/// <see cref="StoreNow"/> and locally measured monotonic elapsed time, never
/// by comparing store timestamps against an application wall clock.
/// </summary>
public sealed record ZLinkOwnerLeaseSnapshot(
    IReadOnlyList<ZLinkOwnerLease> Leases,
    DateTimeOffset StoreNow);

public readonly record struct ZLinkPeerLocationKey(
    ZLinkLocationAutoConnectType AutoConnectType,
    string MeshName,
    ZLinkLocationRole Role,
    RoutingId? NodeRid,
    string? Endpoint);

public readonly record struct ZLinkSpotLocationKey(
    string MeshName,
    RoutingId SpotRid);

/// <summary>
/// The logical messaging address of one spot in a mesh: the owner node and
/// the spot itself. Resolved once through a resolver, held by the caller,
/// and re-resolved on failure — the send path never resolves. An entry
/// spot's address has <c>NodeRid == SpotRid</c>.
/// </summary>
public readonly record struct ZLinkSpotAddress(
    RoutingId NodeRid,
    RoutingId SpotRid);

public readonly record struct ZLinkActorLocationKey(
    string ActorType,
    string ActorId);

public readonly record struct ZLinkRouteLocationKey(
    ZLinkRouteKind RouteKind,
    string RouteKey);

public sealed record ZLinkPeerLocationFilter(
    ZLinkLocationAutoConnectType? AutoConnectType = null,
    string? MeshName = null,
    ZLinkLocationRole? Role = null,
    RoutingId? NodeRid = null,
    string? Endpoint = null);

public sealed record ZLinkSpotLocationFilter(
    string? MeshName = null,
    string? SpotType = null,
    RoutingId? NodeRid = null,
    ZLinkSpotKind? SpotKind = null);

public sealed record ZLinkActorLocationFilter(
    string? ActorType = null,
    RoutingId? NodeRid = null,
    RoutingId? SpotRid = null,
    ZLinkSpotKind? LocationKind = null);

public sealed record ZLinkRouteLocationFilter(
    ZLinkRouteKind? RouteKind = null,
    RoutingId? OwnerNodeRid = null,
    string? OwnerId = null);

/// <summary>
/// Page request for spot/actor/route list queries. The default value means
/// "use the configured default page size from the first page".
/// </summary>
public readonly record struct ZLinkPageRequest(
    int PageSize = 0,
    string? ContinuationToken = null);

public sealed record ZLinkLocationPage<T>(
    IReadOnlyList<T> Items,
    string? ContinuationToken);

public enum ZLinkLocationWriteIntent
{
    NewClaim = 1,
    Renew = 2,
    Takeover = 3
}

public enum ZLinkLocationWriteStatus
{
    Stored = 1,
    IgnoredStale = 2,
    RejectedConflict = 3,
    StoreUnavailable = 4
}

/// <summary>
/// Result of a store write. When <see cref="Status"/> is Stored the store
/// returns the generation it issued and the update time it recorded; the
/// caller keeps them as its owner token. Generations are never distributed
/// between nodes by any other path.
/// </summary>
public sealed record ZLinkLocationWriteResult(
    ZLinkLocationWriteStatus Status,
    long Generation,
    DateTimeOffset UpdatedAt)
{
    public static ZLinkLocationWriteResult IgnoredStale { get; } =
        new(ZLinkLocationWriteStatus.IgnoredStale, 0, default);

    public static ZLinkLocationWriteResult RejectedConflict { get; } =
        new(ZLinkLocationWriteStatus.RejectedConflict, 0, default);

    public static ZLinkLocationWriteResult StoreUnavailable { get; } =
        new(ZLinkLocationWriteStatus.StoreUnavailable, 0, default);

    public static ZLinkLocationWriteResult Stored(long generation, DateTimeOffset updatedAt) =>
        new(ZLinkLocationWriteStatus.Stored, generation, updatedAt);
}

public readonly record struct ZLinkLocationOwnerToken(
    string OwnerId,
    long Generation);

public sealed record ZLinkLocationWatchFilter(
    ZLinkLocationKind Kind,
    string? MeshName = null,
    ZLinkRouteKind? RouteKind = null);

public enum ZLinkLocationChangeType
{
    Upserted = 1,
    Removed = 2,
    Expired = 3
}

public sealed record ZLinkLocationChanged(
    ZLinkLocationKind Kind,
    string LocationKey,
    ZLinkLocationChangeType ChangeType,
    long Generation,
    DateTimeOffset UpdatedAt);

public readonly record struct ZLinkLocationChangeStampScope(
    ZLinkLocationKind Kind,
    string? MeshName);

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
