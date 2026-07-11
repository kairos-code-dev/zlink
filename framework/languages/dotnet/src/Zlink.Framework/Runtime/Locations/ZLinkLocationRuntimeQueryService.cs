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
    private readonly ZLinkObservedLocationGenerations _observed;
    private readonly ZLinkLiveLocationRows _liveRows;
    private readonly bool _watchEnabled;
    private readonly ZLinkLocationStoreHealth? _storeHealth;

    internal ZLinkLocationRuntimeQueryService(
        ZLinkLocationOptions options,
        IZLinkPeerLocationStore peerStore,
        IZLinkSpotLocationStore spotStore,
        IZLinkActorLocationStore actorStore,
        IZLinkRouteLocationStore routeStore,
        ZLinkOwnerLeaseTracker leaseTracker,
        ZLinkLocationRuntime runtime,
        ZLinkObservedLocationGenerations observed,
        bool watchEnabled = false,
        ZLinkLocationStoreHealth? storeHealth = null)
    {
        _options = options;
        _peerStore = peerStore;
        _spotStore = spotStore;
        _actorStore = actorStore;
        _routeStore = routeStore;
        _leaseTracker = leaseTracker;
        _runtime = runtime;
        _observed = observed;
        _watchEnabled = watchEnabled;
        _storeHealth = storeHealth;
        _liveRows = new ZLinkLiveLocationRows(leaseTracker);
    }

    public ValueTask<ZLinkLocationRuntimeStatus> GetStatusAsync(
        CancellationToken cancellationToken = default)
    {
        var health = _runtime.GetHealthSnapshot();
        var store = _storeHealth?.GetSnapshot();
        return ValueTask.FromResult(new ZLinkLocationRuntimeStatus(
            StoreHealthy: health.LastError is null && (store?.Healthy ?? true),
            WatchEnabled: _watchEnabled,
            PollingInterval: _options.PollingInterval,
            LastRefreshAt: store?.LastSuccessAt ?? health.RenewedAt,
            LastError: store?.LastError ?? health.LastError,
            OwnerLeaseHealthy: health.Healthy,
            OwnerLeaseRenewedAt: health.RenewedAt));
    }

    public async ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListPeerLocationsAsync(
        ZLinkPeerLocationFilter filter,
        CancellationToken cancellationToken = default)
    {
        var rows = await ListAcceptedPeersAsync(filter, cancellationToken).ConfigureAwait(false);
        return await _liveRows.FilterAsync(
                rows,
                static row => row.OwnerId,
                static _ => true,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ZLinkLocationPage<ZLinkSpotLocation>> ListSpotLocationsAsync(
        ZLinkSpotLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default)
    {
        var raw = await ZLinkLocationStoreRead.ExecuteAsync(
                _storeHealth,
                "spot-query-read",
                cancellationToken,
                () => _spotStore.ListSpotsAsync(filter, Normalize(page), cancellationToken))
            .ConfigureAwait(false);
        var live = await _liveRows.FilterAsync(
                raw.Items, static row => row.OwnerId, row => _observed.AcceptSpot(row), cancellationToken)
            .ConfigureAwait(false);
        return new ZLinkLocationPage<ZLinkSpotLocation>(live, raw.ContinuationToken);
    }

    public async ValueTask<ZLinkLocationPage<ZLinkActorLocation>> ListActorLocationsAsync(
        ZLinkActorLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default)
    {
        var raw = await ZLinkLocationStoreRead.ExecuteAsync(
                _storeHealth,
                "actor-query-read",
                cancellationToken,
                () => _actorStore.ListActorsAsync(filter, Normalize(page), cancellationToken))
            .ConfigureAwait(false);
        var published = raw.Items.Where(static row => row.ActorRef is not null).ToArray();
        var live = await _liveRows.FilterAsync(
                published, static row => row.OwnerId, row => _observed.AcceptActor(row), cancellationToken)
            .ConfigureAwait(false);
        return new ZLinkLocationPage<ZLinkActorLocation>(live, raw.ContinuationToken);
    }

    public async ValueTask<ZLinkLocationPage<ZLinkRouteLocation>> ListRouteLocationsAsync(
        ZLinkRouteLocationFilter filter,
        ZLinkPageRequest page = default,
        CancellationToken cancellationToken = default)
    {
        var raw = await ZLinkLocationStoreRead.ExecuteAsync(
                _storeHealth,
                "route-query-read",
                cancellationToken,
                () => _routeStore.ListRoutesAsync(filter, Normalize(page), cancellationToken))
            .ConfigureAwait(false);
        var live = await _liveRows.FilterAsync(
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
                var rows = await ListAcceptedPeersAsync(
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

                return PageInMemory(
                    entries.Where(entry => Matches(entry, filter)).ToList(),
                    Normalize(page));
            }

            case ZLinkLocationKind.Spot:
            {
                var raw = await ZLinkLocationStoreRead.ExecuteAsync(
                        _storeHealth,
                        "spot-topology-read",
                        cancellationToken,
                        () => _spotStore.ListSpotsAsync(
                            new ZLinkSpotLocationFilter(MeshName: filter.MeshName, NodeRid: filter.NodeRid),
                            Normalize(page),
                            cancellationToken))
                    .ConfigureAwait(false);
                var entries = new List<ZLinkLocationTopologyEntry>(raw.Items.Count);
                foreach (var row in raw.Items.Where(_observed.AcceptSpot))
                {
                    var live = await _leaseTracker.IsOwnerLiveAsync(row.OwnerId, cancellationToken)
                        .ConfigureAwait(false);
                    var entry = new ZLinkLocationTopologyEntry(
                        ZLinkLocationKind.Spot, row.MeshName, null, row.NodeRid,
                        row.SpotRid, null, row.RouteEndpoint,
                        live ? ZLinkLocationTopologyState.Ready : ZLinkLocationTopologyState.Lost,
                        1, live ? 1u : 0u, 0, row.UpdatedAt);
                    if (Matches(entry, filter)) entries.Add(entry);
                }
                return new ZLinkLocationPage<ZLinkLocationTopologyEntry>(entries, raw.ContinuationToken);
            }

            case ZLinkLocationKind.Actor:
            {
                var raw = await ZLinkLocationStoreRead.ExecuteAsync(
                        _storeHealth,
                        "actor-topology-read",
                        cancellationToken,
                        () => _actorStore.ListActorsAsync(
                            new ZLinkActorLocationFilter(NodeRid: filter.NodeRid),
                            Normalize(page),
                            cancellationToken))
                    .ConfigureAwait(false);
                var entries = new List<ZLinkLocationTopologyEntry>(raw.Items.Count);
                foreach (var row in raw.Items.Where(static row => row.ActorRef is not null)
                             .Where(_observed.AcceptActor))
                {
                    var live = await _leaseTracker.IsOwnerLiveAsync(row.OwnerId, cancellationToken)
                        .ConfigureAwait(false);
                    var entry = new ZLinkLocationTopologyEntry(
                        ZLinkLocationKind.Actor, row.SpotMeshName, null, row.NodeRid,
                        row.SpotRid, row.ActorId, null,
                        live ? ZLinkLocationTopologyState.Ready : ZLinkLocationTopologyState.Lost,
                        1, live ? 1u : 0u, 0, row.UpdatedAt);
                    if (Matches(entry, filter)) entries.Add(entry);
                }
                return new ZLinkLocationPage<ZLinkLocationTopologyEntry>(entries, raw.ContinuationToken);
            }

            default:
            {
                var raw = await ZLinkLocationStoreRead.ExecuteAsync(
                        _storeHealth,
                        "route-topology-read",
                        cancellationToken,
                        () => _routeStore.ListRoutesAsync(
                            new ZLinkRouteLocationFilter(OwnerNodeRid: filter.NodeRid),
                            Normalize(page),
                            cancellationToken))
                    .ConfigureAwait(false);
                var entries = new List<ZLinkLocationTopologyEntry>(raw.Items.Count);
                foreach (var row in raw.Items.Where(_observed.AcceptRoute))
                {
                    var live = await _leaseTracker.IsOwnerLiveAsync(row.OwnerId, cancellationToken)
                        .ConfigureAwait(false);
                    var entry = new ZLinkLocationTopologyEntry(
                        ZLinkLocationKind.Route, null, null, row.OwnerNodeRid,
                        null, null, null,
                        live ? ZLinkLocationTopologyState.Ready : ZLinkLocationTopologyState.Lost,
                        1, live ? 1u : 0u, 0, row.UpdatedAt);
                    if (Matches(entry, filter)) entries.Add(entry);
                }
                return new ZLinkLocationPage<ZLinkLocationTopologyEntry>(entries, raw.ContinuationToken);
            }
        }
    }

    public async ValueTask<IReadOnlyList<ZLinkLocationServiceSummary>> ListServiceSummariesAsync(
        ZLinkLocationServiceSummaryFilter filter,
        CancellationToken cancellationToken = default)
    {
        var rows = await ListAcceptedPeersAsync(
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

    private async ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListAcceptedPeersAsync(
        ZLinkPeerLocationFilter filter,
        CancellationToken cancellationToken)
    {
        var rows = await ZLinkLocationStoreRead.ExecuteAsync(
            _storeHealth,
            "peer-query-read",
            cancellationToken,
            () => _peerStore.ListPeersAsync(filter, cancellationToken)).ConfigureAwait(false);
        return rows.Where(AcceptPeer).ToArray();
    }

    private bool AcceptPeer(ZLinkPeerLocation row)
    {
        if (ZLinkLocationValueCodec.IsKnown(row.AutoConnectType)
            && ZLinkLocationValueCodec.IsKnown(row.Role))
            return _observed.AcceptPeer(row);

        ZLinkFrameworkDebugLog.SpotDiscovery(
            $"peer row ignored: unknown auto-connect type '{row.AutoConnectType}' "
            + $"or role '{row.Role}' (mesh '{row.MeshName}', endpoint '{row.Endpoint}')");
        return false;
    }

    private static bool Matches(
        ZLinkLocationTopologyEntry entry,
        ZLinkLocationTopologyFilter filter) =>
        (filter.Kind is null || entry.Kind == filter.Kind)
        && (filter.MeshName is null
            || string.Equals(entry.MeshName, filter.MeshName, StringComparison.Ordinal))
        && (filter.Role is null || entry.Role == filter.Role)
        && (filter.NodeRid is null || entry.NodeRid == filter.NodeRid)
        && (filter.State is null || entry.State == filter.State);

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

    private sealed class Accumulator
    {
        public uint Total;
        public uint Ready;
        public uint Stopped;
        public DateTimeOffset LastUpdatedAt;
    }
}
