namespace Zlink.Framework.Contracts.Locations;

/// <summary>
/// Per-call cache behavior for resolver reads. Normal allows cache hits;
/// Refresh always reads the store and refreshes the cache with the result.
/// Reconnect and create-if-absent decisions must use Refresh.
/// </summary>
public enum ZLinkResolveFreshness
{
    Normal = 0,
    Refresh = 1
}

/// <summary>
/// Peer list read surface for auto connect. This is the only list surface
/// that goes through a cache and accepts freshness; diagnostics use
/// <see cref="IZLinkLocationRuntimeQuery"/> instead.
/// </summary>
public interface IZLinkPeerLocationResolver
{
    ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
        ZLinkPeerLocationFilter filter,
        ZLinkResolveFreshness freshness = ZLinkResolveFreshness.Normal,
        CancellationToken cancellationToken = default);
}

public interface IZLinkSpotLocationResolver
{
    ValueTask<ZLinkSpotLocation?> ResolveSpotAsync(
        ZLinkSpotLocationKey key,
        ZLinkResolveFreshness freshness = ZLinkResolveFreshness.Normal,
        CancellationToken cancellationToken = default);
}

public interface IZLinkActorLocationResolver
{
    ValueTask<ZLinkActorLocation?> ResolveActorAsync(
        ZLinkActorLocationKey key,
        ZLinkResolveFreshness freshness = ZLinkResolveFreshness.Normal,
        CancellationToken cancellationToken = default);
}

public interface IZLinkRouteLocationResolver
{
    ValueTask<ZLinkRouteLocation?> ResolveRouteAsync(
        ZLinkRouteLocationKey key,
        ZLinkResolveFreshness freshness = ZLinkResolveFreshness.Normal,
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
