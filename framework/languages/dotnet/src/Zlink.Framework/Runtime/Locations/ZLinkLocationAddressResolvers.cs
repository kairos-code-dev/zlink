namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Public messaging lookup surfaces: spot rid / actor id to a full spot
/// address. A spot rid is searched across every spot mesh registered on
/// this node (the same names the auto-connect host advertises under); the
/// actor lookup derives the address from the actor row's spot kind — the
/// entry spot address is the node itself. The returned opaque handle keeps
/// its logical lookup key and receives location-event/watch updates without
/// exposing address refresh policy to callers.
/// </summary>
internal sealed class ZLinkLocationAddressResolvers :
    IZLinkSpotHandleResolver,
    IZLinkActorSpotHandleResolver
{
    private readonly ZLinkStoreLocationResolvers _rows;
    private readonly ZLinkSpotMeshLocationResolver _spots;
    private readonly ZLinkSpotHandleRegistry _handles;
    private readonly ZLinkSpotRouterChannelMap _routerChannels;

    internal ZLinkLocationAddressResolvers(
        ZLinkStoreLocationResolvers rows,
        ZLinkSpotMeshLocationResolver spots,
        ZLinkSpotHandleRegistry handles,
        ZLinkSpotRouterChannelMap? routerChannels = null)
    {
        _rows = rows;
        _spots = spots;
        _handles = handles;
        _routerChannels = routerChannels ?? new ZLinkSpotRouterChannelMap(new ZLinkLocationOptions());
    }

    public async ValueTask<SpotHandle?> ResolveSpotHandleAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
    {
        var row = await _spots.ResolveAsync(spotRid, cancellationToken).ConfigureAwait(false);
        if (row is null) return null;
        var key = new ZLinkSpotLocationKey(row.MeshName, row.SpotRid);
        var handle = new ZLinkResolvedSpotHandle(
            ToSnapshot(row),
            ct => RefreshSpotAsync(key, ct));
        _handles.RegisterSpot(key, handle);
        return handle;
    }

    public async ValueTask<SpotHandle?> ResolveActorSpotHandleAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        var row = await _rows.ResolveActorRowAsync(
                new ZLinkActorLocationKey(actorId),
                cancellationToken)
            .ConfigureAwait(false);
        if (row is null)
        {
            return null;
        }

        // Entry actors use their node's Entry Spot snapshot; actors in a user
        // Spot use that Spot's current snapshot.
        var handle = new ZLinkResolvedSpotHandle(
            ToSnapshot(row),
            ct => RefreshActorAsync(actorId, ct));
        _handles.RegisterActor(actorId, handle);
        return handle;
    }

    private async ValueTask<ZLinkSpotHandleSnapshot?> RefreshSpotAsync(
        ZLinkSpotLocationKey key,
        CancellationToken cancellationToken)
    {
        var row = await _rows.ResolveSpotRowAsync(key, cancellationToken).ConfigureAwait(false);
        return row is null ? null : ToSnapshot(row);
    }

    private async ValueTask<ZLinkSpotHandleSnapshot?> RefreshActorAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        var row = await _rows.ResolveActorRowAsync(
                new ZLinkActorLocationKey(actorId), cancellationToken)
            .ConfigureAwait(false);
        return row is null ? null : ToSnapshot(row);
    }

    private ZLinkSpotHandleSnapshot ToSnapshot(ZLinkSpotLocation row)
        => new(_routerChannels.Resolve(row.MeshName), row.NodeRid, row.SpotRid, row.Generation, row.SpotKind);

    internal ZLinkSpotHandleSnapshot ToSnapshot(ZLinkActorLocation row)
        => row.LocationKind == ZLinkSpotKind.Entry || row.SpotRid is not { Size: > 0 }
            ? new ZLinkSpotHandleSnapshot(
                _routerChannels.Resolve(row.SpotMeshName),
                row.NodeRid,
                row.NodeRid,
                row.Generation,
                ZLinkSpotKind.Entry)
            : new ZLinkSpotHandleSnapshot(
                _routerChannels.Resolve(row.SpotMeshName),
                row.NodeRid,
                row.SpotRid.Value,
                row.Generation,
                ZLinkSpotKind.User);
}

internal readonly record struct ZLinkSpotHandleSnapshot(
    string RouterChannelId,
    RoutingId NodeRid,
    RoutingId SpotRid,
    long Generation,
    ZLinkSpotKind SpotKind = ZLinkSpotKind.User);

internal sealed class ZLinkResolvedSpotHandle : SpotHandle
{
    private readonly Func<CancellationToken, ValueTask<ZLinkSpotHandleSnapshot?>> _refresh;
    private readonly object _gate = new();
    private ZLinkHandleAvailability _availability = ZLinkHandleAvailability.Available;
    private long _generation;
    private ZLinkSpotHandleSnapshot _snapshot;

    internal ZLinkResolvedSpotHandle(
        ZLinkSpotHandleSnapshot initialSnapshot,
        Func<CancellationToken, ValueTask<ZLinkSpotHandleSnapshot?>> refresh)
    {
        _refresh = refresh;
        _snapshot = initialSnapshot;
        _generation = initialSnapshot.Generation;
    }

    internal long CurrentGeneration { get { lock (_gate) return _generation; } }

    internal ZLinkSpotHandleSnapshot Snapshot
    {
        get
        {
            lock (_gate)
            {
                if (_availability != ZLinkHandleAvailability.Available)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.SpotRouteNotFound,
                        "The resolved spot handle is no longer available.");
                return _snapshot;
            }
        }
    }

    public override RoutingId SpotRid { get { lock (_gate) return _snapshot.SpotRid; } }

    internal void Update(ZLinkSpotHandleSnapshot snapshot)
    {
        lock (_gate)
        {
            if (snapshot.Generation < _generation
                || (snapshot.Generation == _generation
                    && _availability == ZLinkHandleAvailability.Removed)) return;
            _snapshot = snapshot;
            _generation = snapshot.Generation;
            _availability = ZLinkHandleAvailability.Available;
        }
    }

    internal void Invalidate(long generation)
    {
        lock (_gate)
        {
            if (generation < _generation) return;
            _generation = generation;
            _availability = ZLinkHandleAvailability.Removed;
        }
    }

    internal void MarkPending(long generation)
    {
        lock (_gate)
        {
            if (generation < _generation
                || (generation == _generation
                    && _availability == ZLinkHandleAvailability.Removed)) return;
            _generation = generation;
            _availability = ZLinkHandleAvailability.PendingUpsert;
        }
    }

    internal void InvalidateIfGeneration(long generation)
    {
        lock (_gate)
        {
            if (_generation == generation
                && _availability != ZLinkHandleAvailability.Removed)
                _availability = ZLinkHandleAvailability.PendingUpsert;
        }
    }

    internal async ValueTask<bool> RefreshAsync(CancellationToken cancellationToken)
    {
        var refreshed = await _refresh(cancellationToken).ConfigureAwait(false);
        if (refreshed is not { } snapshot) return false;
        Update(snapshot);
        return true;
    }
}

internal enum ZLinkHandleAvailability
{
    Available,
    PendingUpsert,
    Removed
}

internal static class ZLinkSpotHandleRequestExecution
{
    internal static async ValueTask<T> ExecuteAsync<T>(
        ZLinkResolvedSpotHandle handle,
        Func<ZLinkSpotHandleSnapshot, ValueTask<T>> operation,
        CancellationToken cancellationToken)
    {
        try
        {
            return await operation(handle.Snapshot).ConfigureAwait(false);
        }
        catch (ZLinkFrameworkException error) when (IsRefreshCandidate(error))
        {
            if (!await handle.RefreshAsync(cancellationToken).ConfigureAwait(false)) throw;
            return await operation(handle.Snapshot).ConfigureAwait(false);
        }
    }

    private static bool IsRefreshCandidate(ZLinkFrameworkException error)
        => error.Kind is ZLinkFrameworkErrorKind.SpotRouteNotFound
            or ZLinkFrameworkErrorKind.RequestTargetNotFound;
}
