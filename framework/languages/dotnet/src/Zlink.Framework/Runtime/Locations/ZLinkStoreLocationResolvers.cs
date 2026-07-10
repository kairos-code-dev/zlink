namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Default resolvers reading the registered stores. Every read reaches the
/// store — there is no resolver cache (spot-address messaging draft §8) —
/// and joins owner liveness plus the monotonic generation guard before a
/// row counts as live. Spot and actor rows are exposed internally for the
/// address resolvers and lifecycle flows; the public messaging surfaces
/// live on <see cref="ZLinkLocationAddressResolvers"/>.
/// </summary>
internal sealed class ZLinkStoreLocationResolvers :
    IZLinkPeerLocationResolver
{
    private readonly IZLinkPeerLocationStore _peerStore;
    private readonly IZLinkSpotLocationStore _spotStore;
    private readonly IZLinkActorLocationStore _actorStore;
    private readonly IZLinkRouteLocationStore _routeStore;
    private readonly ZLinkLocationEventEmitter _events;
    private readonly ZLinkObservedLocationGenerations _observed;
    private readonly ZLinkLiveLocationRows _liveRows;

    internal ZLinkStoreLocationResolvers(
        IZLinkPeerLocationStore peerStore,
        IZLinkSpotLocationStore spotStore,
        IZLinkActorLocationStore actorStore,
        IZLinkRouteLocationStore routeStore,
        ZLinkOwnerLeaseTracker leaseTracker,
        ZLinkObservedLocationGenerations observed,
        ZLinkLocationEventEmitter? events = null)
    {
        _peerStore = peerStore;
        _spotStore = spotStore;
        _actorStore = actorStore;
        _routeStore = routeStore;
        _events = events ?? ZLinkLocationEventEmitter.Disabled;
        _observed = observed;
        _liveRows = new ZLinkLiveLocationRows(leaseTracker);
    }

    public async ValueTask<IReadOnlyList<ZLinkPeerLocation>> ListLivePeersAsync(
        ZLinkPeerLocationFilter filter,
        CancellationToken cancellationToken = default)
    {
        var rows = await _peerStore.ListPeersAsync(filter, cancellationToken).ConfigureAwait(false);

        // Drop rows older than a generation this runtime already observed
        // for the same key: a lagging store replica must never roll the
        // view backwards (monotonic read guard). Rows with values outside
        // the closed auto-connect/role sets are ignored too and reported to
        // diagnostics (draft 6.5).
        return await _liveRows.FilterAsync(
                rows,
                static row => row.OwnerId,
                AcceptPeer,
                cancellationToken)
            .ConfigureAwait(false);
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

    internal async ValueTask<ZLinkSpotLocation?> ResolveSpotRowAsync(
        ZLinkSpotLocationKey key,
        CancellationToken cancellationToken = default)
    {
        var row = await ResolveAsync(
            key,
            static (store, key, ct) => store.ResolveSpotAsync(key, ct),
            _spotStore,
            static row => row.OwnerId,
            row => _observed.AcceptSpot(row),
            cancellationToken).ConfigureAwait(false);
        if (row is null)
        {
            await _events.SpotResolveMissAsync(key, cancellationToken).ConfigureAwait(false);
        }

        return row;
    }

    internal async ValueTask<ZLinkActorLocation?> ResolveActorRowAsync(
        ZLinkActorLocationKey key,
        CancellationToken cancellationToken = default)
    {
        var row = await ResolveAsync(
            key,
            static (store, key, ct) => store.ResolveActorAsync(key, ct),
            _actorStore,
            static row => row.OwnerId,
            row => _observed.AcceptActor(row),
            cancellationToken).ConfigureAwait(false);
        if (row is null)
        {
            await _events.ActorResolveMissAsync(key, cancellationToken).ConfigureAwait(false);
            return null;
        }

        if (row.ActorRef is null)
        {
            await _events.ActorResolveMissAsync(key, cancellationToken).ConfigureAwait(false);
            return null;
        }

        return row;
    }

    internal async ValueTask<ZLinkRouteLocation?> ResolveRouteAsync(
        ZLinkRouteLocationKey key,
        CancellationToken cancellationToken = default)
    {
        var row = await ResolveAsync(
            key,
            static (store, key, ct) => store.ResolveRouteAsync(key, ct),
            _routeStore,
            static row => row.OwnerId,
            row => _observed.AcceptRoute(row),
            cancellationToken).ConfigureAwait(false);
        if (row is null)
        {
            await _events.RouteResolveMissAsync(key, cancellationToken).ConfigureAwait(false);
        }

        return row;
    }

    private async ValueTask<TRow?> ResolveAsync<TStore, TKey, TRow>(
        TKey key,
        Func<TStore, TKey, CancellationToken, ValueTask<TRow?>> resolve,
        TStore store,
        Func<TRow, string> ownerOf,
        Func<TRow, bool> acceptObserved,
        CancellationToken cancellationToken)
        where TKey : notnull
        where TRow : class
    {
        return await _liveRows.ResolveAsync(
                await resolve(store, key, cancellationToken).ConfigureAwait(false),
                ownerOf,
                acceptObserved,
                cancellationToken)
            .ConfigureAwait(false);
    }
}
