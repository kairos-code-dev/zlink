namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Operational read surface. Every query reads the registered stores
/// directly — no cache is consulted or written, which is why this surface
/// takes no freshness. Rows whose owner lease expired and rows older than a
/// generation this runtime already observed are filtered out of success
/// results everywhere (draft 8.1).
/// </summary>
internal sealed class ZLinkLocationRuntimeQueryService : IZLinkLocationRuntimeQuery
{
    private readonly ZLinkLocationOptions _options;
    private readonly IZLinkPeerLocationStore _peerStore;
    private readonly IZLinkSpotLocationStore _spotStore;
    private readonly IZLinkActorLocationStore _actorStore;
    private readonly IZLinkRouteLocationStore _routeStore;
    private readonly ZLinkOwnerLeaseTracker _leaseTracker;
    private readonly ZLinkLocationRuntime _runtime;
    private readonly ZLinkStoreLocationResolvers _resolvers;
    private readonly ZLinkObservedLocationGenerations _observed;

    internal ZLinkLocationRuntimeQueryService(
        ZLinkLocationOptions options,
        IZLinkPeerLocationStore peerStore,
        IZLinkSpotLocationStore spotStore,
        IZLinkActorLocationStore actorStore,
        IZLinkRouteLocationStore routeStore,
        ZLinkOwnerLeaseTracker leaseTracker,
        ZLinkLocationRuntime runtime,
        ZLinkStoreLocationResolvers resolvers,
        ZLinkObservedLocationGenerations? observed = null)
    {
        _options = options;
        _peerStore = peerStore;
        _spotStore = spotStore;
        _actorStore = actorStore;
        _routeStore = routeStore;
        _leaseTracker = leaseTracker;
        _runtime = runtime;
        _resolvers = resolvers;
        _observed = observed ?? new ZLinkObservedLocationGenerations();
    }

    public ValueTask<ZLinkLocationRuntimeStatus> GetStatusAsync(
        CancellationToken cancellationToken = default) =>
        ValueTask.FromResult(new ZLinkLocationRuntimeStatus(
            StoreHealthy: _runtime.LastError is null,
            WatchEnabled: false,
            PollingInterval: _options.PollingInterval,
            LastRefreshAt: _runtime.OwnerLeaseRenewedAt,
            LastError: _runtime.LastError,
            OwnerLeaseHealthy: _runtime.OwnerLeaseHealthy,
            OwnerLeaseRenewedAt: _runtime.OwnerLeaseRenewedAt));

    public async ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeersAsync(
        ZLinkPeerLocationFilter filter,
        CancellationToken cancellationToken = default)
    {
        var rows = await _peerStore.ListPeersAsync(filter, cancellationToken).ConfigureAwait(false);
        return await FilterLiveAsync(
                rows, static row => row.OwnerId, row => _observed.AcceptPeer(row), cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ZLinkLocationPage<ZLinkSpotLocation>> ListSpotsAsync(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default)
    {
        var raw = await _spotStore.ListSpotsAsync(filter, Normalize(page), cancellationToken)
            .ConfigureAwait(false);
        var live = await FilterLiveAsync(
                raw.Items, static row => row.OwnerId, row => _observed.AcceptSpot(row), cancellationToken)
            .ConfigureAwait(false);
        return new ZLinkLocationPage<ZLinkSpotLocation>(live, raw.ContinuationToken);
    }

    public async ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorsAsync(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default)
    {
        var raw = await _actorStore.ListActorsAsync(filter, Normalize(page), cancellationToken)
            .ConfigureAwait(false);
        var live = await FilterLiveAsync(
                raw.Items, static row => row.OwnerId, row => _observed.AcceptActor(row), cancellationToken)
            .ConfigureAwait(false);
        return new ZLinkLocationPage<ZLinkActorLocation>(live, raw.ContinuationToken);
    }

    public async ValueTask<ZLinkLocationPage<ZLinkRouteLocation>> ListRoutesAsync(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default)
    {
        var raw = await _routeStore.ListRoutesAsync(filter, Normalize(page), cancellationToken)
            .ConfigureAwait(false);
        var live = await FilterLiveAsync(
                raw.Items, static row => row.OwnerId, row => _observed.AcceptRoute(row), cancellationToken)
            .ConfigureAwait(false);
        return new ZLinkLocationPage<ZLinkRouteLocation>(live, raw.ContinuationToken);
    }

    public async ValueTask<ZLinkLocationPage<ZLinkLocationTopologyEntry>> ListTopologyAsync(
        ZLinkLocationTopologyFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default)
    {
        // The projection is built by the framework from location rows plus
        // liveness; stores never decide topology meaning. Connection state
        // integration arrives with the auto-connect runtime; until a row's
        // owner lease expires it reports Ready, afterwards Lost.
        var kind = filter.Kind ?? ZLinkLocationKind.Peer;
        switch (kind)
        {
            case ZLinkLocationKind.Peer:
            {
                var rows = await _peerStore.ListPeersAsync(
                    new ZLinkPeerLocationFilter(MeshName: filter.MeshName, Role: filter.Role, NodeRid: filter.NodeRid),
                    cancellationToken).ConfigureAwait(false);
                var entries = new List<ZLinkLocationTopologyEntry>(rows.Count);
                foreach (var row in rows)
                {
                    var live = await _leaseTracker.IsOwnerLiveAsync(row.OwnerId, cancellationToken)
                        .ConfigureAwait(false);
                    var state = live ? ZLinkLocationTopologyState.Ready : ZLinkLocationTopologyState.Lost;
                    if (filter.State is not null && state != filter.State)
                    {
                        continue;
                    }

                    entries.Add(new ZLinkLocationTopologyEntry(
                        ZLinkLocationKind.Peer, row.MeshName, row.Role, row.NodeRid,
                        null, null, row.Endpoint, state, 1, live ? 1u : 0u, 0, row.UpdatedAt));
                }

                return PageInMemory(entries, Normalize(page));
            }

            case ZLinkLocationKind.Spot:
            {
                var spots = await ListSpotsAsync(
                    new ZLinkSpotLocationFilter(MeshName: filter.MeshName, NodeRid: filter.NodeRid),
                    Normalize(page),
                    cancellationToken).ConfigureAwait(false);
                var entries = spots.Items
                    .Select(row => new ZLinkLocationTopologyEntry(
                        ZLinkLocationKind.Spot, row.MeshName, null, row.NodeRid,
                        row.SpotRid, null, row.RouteEndpoint, ZLinkLocationTopologyState.Ready,
                        1, 1, 0, row.UpdatedAt))
                    .ToArray();
                return new ZLinkLocationPage<ZLinkLocationTopologyEntry>(entries, spots.ContinuationToken);
            }

            case ZLinkLocationKind.Actor:
            {
                var actors = await ListActorsAsync(
                    new ZLinkActorLocationFilter(NodeRid: filter.NodeRid),
                    Normalize(page),
                    cancellationToken).ConfigureAwait(false);
                var entries = actors.Items
                    .Select(row => new ZLinkLocationTopologyEntry(
                        ZLinkLocationKind.Actor, null, null, row.NodeRid,
                        row.SpotRid, row.ActorId, null, ZLinkLocationTopologyState.Ready,
                        1, 1, 0, row.UpdatedAt))
                    .ToArray();
                return new ZLinkLocationPage<ZLinkLocationTopologyEntry>(entries, actors.ContinuationToken);
            }

            default:
            {
                var routes = await ListRoutesAsync(
                    new ZLinkRouteLocationFilter(OwnerNodeRid: filter.NodeRid),
                    Normalize(page),
                    cancellationToken).ConfigureAwait(false);
                var entries = routes.Items
                    .Select(row => new ZLinkLocationTopologyEntry(
                        ZLinkLocationKind.Route, null, null, row.OwnerNodeRid,
                        null, null, null, ZLinkLocationTopologyState.Ready,
                        1, 1, 0, row.UpdatedAt))
                    .ToArray();
                return new ZLinkLocationPage<ZLinkLocationTopologyEntry>(entries, routes.ContinuationToken);
            }
        }
    }

    public async ValueTask<IReadOnlyList<ZLinkLocationServiceSummary>> ListServiceSummariesAsync(
        ZLinkLocationServiceSummaryFilter filter,
        CancellationToken cancellationToken = default)
    {
        var rows = await _peerStore.ListPeersAsync(
            new ZLinkPeerLocationFilter(
                AutoConnectType: filter.AutoConnectType,
                MeshName: filter.MeshName,
                Role: filter.Role),
            cancellationToken).ConfigureAwait(false);

        var groups = new Dictionary<(string Mesh, ZLinkLocationAutoConnectType Type, ZLinkLocationRole Role), Accumulator>();
        foreach (var row in rows)
        {
            var key = (row.MeshName, row.AutoConnectType, row.Role);
            if (!groups.TryGetValue(key, out var accumulator))
            {
                accumulator = new Accumulator();
                groups[key] = accumulator;
            }

            accumulator.Total++;
            if (await _leaseTracker.IsOwnerLiveAsync(row.OwnerId, cancellationToken).ConfigureAwait(false))
            {
                accumulator.Ready++;
            }
            else
            {
                accumulator.Stopped++;
            }

            if (row.UpdatedAt > accumulator.LastUpdatedAt)
            {
                accumulator.LastUpdatedAt = row.UpdatedAt;
            }
        }

        return groups
            .Select(pair => new ZLinkLocationServiceSummary(
                pair.Key.Mesh, pair.Key.Type, pair.Key.Role,
                pair.Value.Total, pair.Value.Ready, 0, pair.Value.Stopped,
                pair.Value.LastUpdatedAt))
            .ToArray();
    }

    private ZLinkPageRequest Normalize(ZLinkPageRequest page) =>
        page.PageSize > 0 ? page : page with { PageSize = _options.ListPageSize };

    private static ZLinkLocationPage<T> PageInMemory<T>(List<T> entries, ZLinkPageRequest page)
    {
        var offset = 0;
        if (page.ContinuationToken is { } token && int.TryParse(token, out var parsed))
        {
            offset = parsed;
        }

        var items = entries.Skip(offset).Take(page.PageSize).ToArray();
        var nextOffset = offset + items.Length;
        return new ZLinkLocationPage<T>(
            items,
            nextOffset < entries.Count ? nextOffset.ToString() : null);
    }

    private async ValueTask<IReadOnlyList<TRow>> FilterLiveAsync<TRow>(
        IReadOnlyList<TRow> rows,
        Func<TRow, string> ownerOf,
        Func<TRow, bool> acceptObserved,
        CancellationToken cancellationToken)
    {
        var live = new List<TRow>(rows.Count);
        foreach (var row in rows)
        {
            if (!acceptObserved(row))
            {
                // A lagging store replica returned a generation this runtime
                // already saw superseded; stale rows never count as success.
                continue;
            }

            if (await _leaseTracker.IsOwnerLiveAsync(ownerOf(row), cancellationToken)
                    .ConfigureAwait(false))
            {
                live.Add(row);
            }
        }

        return live;
    }

    private sealed class Accumulator
    {
        public uint Total;
        public uint Ready;
        public uint Stopped;
        public DateTimeOffset LastUpdatedAt;
    }
}
