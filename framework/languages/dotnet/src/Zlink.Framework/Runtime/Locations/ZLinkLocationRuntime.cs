namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Owns this runtime instance's location identity and lifecycle writes.
/// One owner lease heartbeat per interval keeps every row of this owner
/// alive; location rows themselves are written only on lifecycle changes.
/// A write that comes back IgnoredStale means another owner replaced the
/// row: subscribers must stop advertising and deactivate the local
/// instance (ownership-loss rule).
/// </summary>
internal sealed class ZLinkLocationRuntime : IAsyncDisposable, IDisposable
{
    private readonly ZLinkLocationOptions _options;
    private readonly IZLinkLocationStore _locationStore;
    private readonly IZLinkPeerLocationStore _peerStore;
    private readonly IZLinkSpotLocationStore _spotStore;
    private readonly IZLinkActorLocationStore _actorStore;
    private readonly IZLinkRouteLocationStore _routeStore;
    private readonly IZLinkOwnerLeaseStore _ownerLeaseStore;
    private readonly ZLinkLocationEventEmitter _events;
    private readonly TimeProvider _time;
    private readonly object _stateGate = new();
    private CancellationTokenSource? _heartbeatCts;
    private Task? _heartbeatLoop;
    private RoutingId _nodeRid;
    private bool _started;

    internal ZLinkLocationRuntime(
        ZLinkLocationOptions options,
        IZLinkLocationStore locationStore,
        IZLinkPeerLocationStore peerStore,
        IZLinkSpotLocationStore spotStore,
        IZLinkActorLocationStore actorStore,
        IZLinkRouteLocationStore routeStore,
        IZLinkOwnerLeaseStore ownerLeaseStore,
        TimeProvider? timeProvider = null,
        ZLinkLocationEventEmitter? events = null)
    {
        _options = options;
        _locationStore = locationStore;
        _peerStore = peerStore;
        _spotStore = spotStore;
        _actorStore = actorStore;
        _routeStore = routeStore;
        _ownerLeaseStore = ownerLeaseStore;
        _events = events ?? ZLinkLocationEventEmitter.Disabled;
        _time = timeProvider ?? TimeProvider.System;
    }

    /// <summary>Stable process-local owner id, created once per runtime
    /// start. A restarted process gets a fresh id.</summary>
    internal string OwnerId { get; } = Guid.NewGuid().ToString("n");

    internal bool OwnerLeaseHealthy { get; private set; }

    internal DateTimeOffset? OwnerLeaseRenewedAt { get; private set; }

    internal string? LastError { get; private set; }

    /// <summary>Raised when a write for a row this owner believed it owned
    /// came back IgnoredStale. The argument is the canonical location key.</summary>
    internal event Action<ZLinkLocationKind, string>? OwnershipLost;

    internal async ValueTask StartAsync(
        RoutingId nodeRid,
        CancellationToken cancellationToken = default)
    {
        if (_started)
        {
            return;
        }

        _started = true;
        _nodeRid = nodeRid;

        // Register liveness before any row write so readers joining rows
        // against the lease never see this owner's rows as stale on start.
        await RenewOwnerLeaseOnceAsync(cancellationToken).ConfigureAwait(false);
        _heartbeatCts = new CancellationTokenSource();
        _heartbeatLoop = Task.Run(
            () => HeartbeatLoopAsync(_heartbeatCts.Token),
            CancellationToken.None);
    }

    internal async ValueTask StopAsync(CancellationToken cancellationToken = default)
    {
        if (!_started)
        {
            return;
        }

        _started = false;
        if (_heartbeatCts is not null)
        {
            await _heartbeatCts.CancelAsync().ConfigureAwait(false);
        }

        if (_heartbeatLoop is not null)
        {
            try
            {
                await _heartbeatLoop.ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
            }
        }

        // Shutdown order: drop the lease first so every row of this owner
        // turns stale at once, then bulk-remove the rows without a call per
        // row.
        try
        {
            await _ownerLeaseStore.RemoveOwnerLeaseAsync(OwnerId, cancellationToken)
                .ConfigureAwait(false);
            await _locationStore.RemoveAllByOwnerAsync(OwnerId, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            // A store outage during shutdown is not fatal: the expired
            // lease makes the leftover rows stale and background cleanup
            // removes them later.
            RecordFailure(exception.Message);
        }
    }

    public async ValueTask DisposeAsync()
    {
        await StopAsync().ConfigureAwait(false);
        _heartbeatCts?.Dispose();
    }

    /// <summary>
    /// Synchronous dispose only cancels the heartbeat loop. Graceful
    /// shutdown (lease removal and bulk row removal) runs in StopAsync via
    /// the hosted service; blocking on store calls here could deadlock a
    /// synchronous container teardown.
    /// </summary>
    public void Dispose()
    {
        _heartbeatCts?.Cancel();
        _heartbeatCts?.Dispose();
        _heartbeatCts = null;
    }

    internal async ValueTask<ZLinkLocationWriteResult> WritePeerAsync(
        ZLinkPeerLocation peer,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default)
    {
        // The registration path rejects values outside the closed sets as a
        // validation error; readers additionally ignore such rows (draft 6.5).
        if (!ZLinkLocationValueCodec.IsKnown(peer.AutoConnectType))
        {
            throw new ArgumentOutOfRangeException(
                nameof(peer), peer.AutoConnectType, "Unknown auto-connect type.");
        }

        if (!ZLinkLocationValueCodec.IsKnown(peer.Role))
        {
            throw new ArgumentOutOfRangeException(nameof(peer), peer.Role, "Unknown location role.");
        }

        var stamped = peer with { OwnerId = OwnerId };
        return await ExecuteLocationWriteAsync(
                () => _peerStore.UpdatePeerAsync(stamped, intent, cancellationToken),
                ZLinkLocationKind.Peer,
                ZLinkLocationKeyCodec.EncodePeerKey(
                    new ZLinkPeerLocationKey(
                        peer.AutoConnectType,
                        peer.MeshName,
                        peer.Role,
                        peer.NodeRid,
                        peer.Endpoint)),
                result => _events.PeerRowUpdatedAsync(
                stamped with { Generation = result.Generation, UpdatedAt = result.UpdatedAt },
                cancellationToken))
            .ConfigureAwait(false);
    }

    internal async ValueTask<ZLinkLocationWriteResult> WriteSpotAsync(
        ZLinkSpotLocation spot,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default)
    {
        var stamped = spot with { OwnerId = OwnerId };
        return await ExecuteLocationWriteAsync(
                () => _spotStore.UpdateSpotAsync(stamped, intent, cancellationToken),
                ZLinkLocationKind.Spot,
                ZLinkLocationKeyCodec.EncodeSpotKey(new ZLinkSpotLocationKey(spot.MeshName, spot.SpotRid)),
                result => _events.SpotRowUpdatedAsync(
                stamped with { Generation = result.Generation, UpdatedAt = result.UpdatedAt },
                cancellationToken))
            .ConfigureAwait(false);
    }

    internal async ValueTask<ZLinkLocationWriteResult> WriteActorAsync(
        ZLinkActorLocation actor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default)
    {
        var stamped = actor with
        {
            OwnerId = OwnerId
        };
        return await ExecuteLocationWriteAsync(
                () => _actorStore.UpdateActorAsync(stamped, intent, cancellationToken),
                ZLinkLocationKind.Actor,
                ZLinkLocationKeyCodec.EncodeActorKey(new ZLinkActorLocationKey(stamped.ActorId)),
                result => _events.ActorRowUpdatedAsync(
                stamped with { Generation = result.Generation, UpdatedAt = result.UpdatedAt },
                cancellationToken))
            .ConfigureAwait(false);
    }

    internal async ValueTask<ZLinkLocationWriteResult> WriteRouteAsync(
        ZLinkRouteLocation route,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default)
    {
        var stamped = route with { OwnerId = OwnerId };
        return await ExecuteLocationWriteAsync(
                () => _routeStore.UpdateRouteAsync(stamped, intent, cancellationToken),
                ZLinkLocationKind.Route,
                ZLinkLocationKeyCodec.EncodeRouteKey(new ZLinkRouteLocationKey(route.RouteKind, route.RouteKey)),
                result => _events.RouteRowUpdatedAsync(
                stamped with { Generation = result.Generation, UpdatedAt = result.UpdatedAt },
                cancellationToken))
            .ConfigureAwait(false);
    }

    internal async ValueTask<ZLinkLocationWriteResult> RemoveSpotAsync(
        ZLinkSpotLocationKey key,
        long generation,
        CancellationToken cancellationToken = default)
    {
        return await ExecuteLocationWriteAsync(
                () => _spotStore.RemoveSpotAsync(
                    key,
                    new ZLinkLocationOwnerToken(OwnerId, generation),
                    cancellationToken),
                ZLinkLocationKind.Spot,
                ZLinkLocationKeyCodec.EncodeSpotKey(key),
                _ => _events.SpotRowRemovedAsync(key, cancellationToken))
            .ConfigureAwait(false);
    }

    internal async ValueTask<ZLinkLocationWriteResult> RemoveActorAsync(
        ZLinkActorLocationKey key,
        long generation,
        CancellationToken cancellationToken = default)
    {
        return await ExecuteLocationWriteAsync(
                () => _actorStore.RemoveActorAsync(
                    key,
                    new ZLinkLocationOwnerToken(OwnerId, generation),
                    cancellationToken),
                ZLinkLocationKind.Actor,
                ZLinkLocationKeyCodec.EncodeActorKey(key),
                _ => _events.ActorRowRemovedAsync(key, cancellationToken))
            .ConfigureAwait(false);
    }

    internal async ValueTask<ZLinkLocationWriteResult> RemovePeerAsync(
        ZLinkPeerLocationKey key,
        long generation,
        CancellationToken cancellationToken = default)
    {
        return await ExecuteLocationWriteAsync(
                () => _peerStore.RemovePeerAsync(
                    key,
                    new ZLinkLocationOwnerToken(OwnerId, generation),
                    cancellationToken),
                ZLinkLocationKind.Peer,
                ZLinkLocationKeyCodec.EncodePeerKey(key),
                _ => _events.PeerRowRemovedAsync(key, cancellationToken))
            .ConfigureAwait(false);
    }

    internal async ValueTask<ZLinkLocationWriteResult> RemoveRouteAsync(
        ZLinkRouteLocationKey key,
        long generation,
        CancellationToken cancellationToken = default)
    {
        return await ExecuteLocationWriteAsync(
                () => _routeStore.RemoveRouteAsync(
                    key,
                    new ZLinkLocationOwnerToken(OwnerId, generation),
                    cancellationToken),
                ZLinkLocationKind.Route,
                ZLinkLocationKeyCodec.EncodeRouteKey(key),
                _ => _events.RouteRowRemovedAsync(key, cancellationToken))
            .ConfigureAwait(false);
    }

    internal async ValueTask<bool> RenewOwnerLeaseOnceAsync(
        CancellationToken cancellationToken = default)
    {
        try
        {
            var result = await _ownerLeaseStore.RenewOwnerLeaseAsync(
                OwnerId, _nodeRid, _options.OwnerLeaseTtl, cancellationToken)
                .ConfigureAwait(false);
            lock (_stateGate)
            {
                OwnerLeaseHealthy = true;
                OwnerLeaseRenewedAt = result.StoreNow;
                LastError = null;
            }

            return true;
        }
        catch (Exception exception)
        {
            // Fail-static: record the failure and retry on the next tick.
            // Existing rows stay valid until the lease actually expires.
            RecordFailure(exception.Message);
            return false;
        }
    }

    private async Task HeartbeatLoopAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                await Task.Delay(_options.HeartbeatInterval, _time, cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                return;
            }

            await RenewOwnerLeaseOnceAsync(cancellationToken).ConfigureAwait(false);
        }
    }

    private async ValueTask<ZLinkLocationWriteResult> GuardStoreWriteAsync(
        Func<ValueTask<ZLinkLocationWriteResult>> write)
    {
        try
        {
            return await write().ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            RecordFailure(exception.Message);
            throw;
        }
    }

    private async ValueTask<ZLinkLocationWriteResult> ExecuteLocationWriteAsync(
        Func<ValueTask<ZLinkLocationWriteResult>> write,
        ZLinkLocationKind kind,
        string canonicalKey,
        Func<ZLinkLocationWriteResult, ValueTask> emitStored)
    {
        var result = await GuardStoreWriteAsync(write).ConfigureAwait(false);
        NotifyIfStale(result, kind, canonicalKey);
        if (result.Status == ZLinkLocationWriteStatus.Stored)
        {
            await emitStored(result).ConfigureAwait(false);
        }

        return result;
    }

    private void NotifyIfStale(
        ZLinkLocationWriteResult result,
        ZLinkLocationKind kind,
        string canonicalKey)
    {
        if (result.Status == ZLinkLocationWriteStatus.IgnoredStale)
        {
            OwnershipLost?.Invoke(kind, canonicalKey);
        }
    }

    private void RecordFailure(string message)
    {
        lock (_stateGate)
        {
            OwnerLeaseHealthy = false;
            LastError = message;
        }
    }
}
