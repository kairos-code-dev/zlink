namespace Zlink.Framework.Contracts.Locations;

/// <summary>
/// Peer list read surface for auto connect. Every read reaches the store
/// and joins owner liveness; there is no resolver cache. Diagnostics use
/// <see cref="IZLinkLocationRuntimeQuery"/> instead.
/// </summary>
public interface IZLinkPeerLocationResolver
{
    ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
        ZLinkPeerLocationFilter filter,
        CancellationToken cancellationToken = default);
}

/// <summary>
/// Messaging lookup: spot rid to its full address. Callers resolve once,
/// hold the address for the spot's lifecycle, and re-resolve on failure.
/// Lifecycle flows that need generations read location rows through the
/// store/runtime surfaces instead.
/// </summary>
public interface IZLinkSpotLocationResolver
{
    /// <summary>Null when no live row exists (unknown spot or expired owner lease).</summary>
    ValueTask<ZLinkSpotAddress?> ResolveSpotAddressAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default);
}

/// <summary>
/// Messaging lookup: actor id to the full address of the spot it lives on
/// (the entry spot address for ENTRY_SPOT actors, the user spot address for
/// USER_SPOT actors).
/// </summary>
public interface IZLinkActorLocationResolver
{
    ValueTask<ZLinkSpotAddress?> ResolveActorSpotAddressAsync(
        string actorType,
        string actorId,
        CancellationToken cancellationToken = default);
}

public interface IZLinkRouteLocationResolver
{
    ValueTask<ZLinkRouteLocation?> ResolveRouteAsync(
        ZLinkRouteLocationKey key,
        CancellationToken cancellationToken = default);
}

/// <summary>
/// Operational read surface for tools and E2E. Every query reads the store
/// directly without any cache, so no freshness parameter exists here.
/// Spot/actor/route list queries live only on this surface.
/// </summary>
public interface IZLinkLocationRuntimeQuery
{
    ValueTask<ZLinkLocationRuntimeStatus> GetStatusAsync(
        CancellationToken cancellationToken = default);

    ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
        ZLinkPeerLocationFilter filter,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationPage<ZLinkSpotLocation>> ListSpotsAsync(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorsAsync(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationPage<ZLinkRouteLocation>> ListRoutesAsync(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);

    ValueTask<ZLinkLocationPage<ZLinkLocationTopologyEntry>> ListTopologyAsync(
        ZLinkLocationTopologyFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default);

    ValueTask<IReadOnlyList<ZLinkLocationServiceSummary>> ListServiceSummariesAsync(
        ZLinkLocationServiceSummaryFilter filter,
        CancellationToken cancellationToken = default);
}
