using Zlink.Framework.Internal.Locations;

namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Owns this runtime instance's location identity and lifecycle writes.
/// One owner lease heartbeat per interval keeps every row of this owner
/// alive; location rows themselves are written only on lifecycle changes.
/// A write that comes back IgnoredStale means another owner replaced the
/// row: subscribers must stop advertising and deactivate the local
/// instance (ownership-loss rule).
/// </summary>
internal sealed class ZLinkLocationRuntime : IAsyncDisposable
{
    private readonly ZLinkLocationOptions _options;
    private readonly IZLinkLocationStore _locationStore;
    private readonly IZLinkMeshNodeLocationStore _meshNodeStore;
    private readonly IZLinkSpotLocationStore _spotStore;
    private readonly IZLinkActorLocationStore _actorStore;
    private readonly IZLinkOwnerLeaseStore _ownerLeaseStore;
    private readonly ZLinkLocationEventEmitter _events;
    private readonly TimeProvider _time;
    private readonly SemaphoreSlim _lifecycleGate = new(1, 1);
    private readonly object _disposeStartGate = new();
    private ZLinkLocationRuntimeHealth _health = new(false, null, null, null);
    private CancellationTokenSource? _heartbeatCts;
    private Task? _heartbeatLoop;
    private RoutingId _nodeRid;
    private string _ownerId = Guid.NewGuid().ToString("n");
    private ZLinkLocationOwnerToken? _ownerToken;
    private bool _started;
    private bool _ownerCleanedForDrain;
    private int _disposeState;
    private Task? _disposeTask;

    internal ZLinkLocationRuntime(
        ZLinkLocationOptions options,
        IZLinkLocationStore locationStore,
        IZLinkMeshNodeLocationStore meshNodeStore,
        IZLinkSpotLocationStore spotStore,
        IZLinkActorLocationStore actorStore,
        IZLinkOwnerLeaseStore ownerLeaseStore,
        TimeProvider? timeProvider = null,
        ZLinkLocationEventEmitter? events = null)
    {
        _options = options;
        _locationStore = locationStore;
        _meshNodeStore = meshNodeStore;
        _spotStore = spotStore;
        _actorStore = actorStore;
        _ownerLeaseStore = ownerLeaseStore;
        _events = events ?? ZLinkLocationEventEmitter.Disabled;
        _time = timeProvider ?? TimeProvider.System;
    }

    /// <summary>Stable owner id for the current runtime generation. Each
    /// successful restart attempt uses a fresh id so rows left by an older
    /// generation cannot become live again.</summary>
    internal string OwnerId => _ownerId;

    internal ZLinkLocationOwnerToken OwnerToken =>
        _ownerToken
        ?? throw new InvalidOperationException(
            "The location runtime has not claimed its owner lease.");

    internal string? LastError => Volatile.Read(ref _health).LastError;

    internal ZLinkLocationRuntimeHealth GetHealthSnapshot() => Volatile.Read(ref _health);

    /// <summary>Raised when a write for a row this owner believed it owned
    /// came back IgnoredStale. The argument is the canonical location key.</summary>
    internal event Action<ZLinkLocationKind, string>? OwnershipLost;

    internal event Action<ZLinkOwnerLeaseRenewal>? OwnerLeaseRenewed;

    internal event Action? OwnerLeaseRenewalFailed;

    internal async ValueTask StartAsync(
        RoutingId nodeRid,
        CancellationToken cancellationToken = default)
    {
        ThrowIfDisposingOrDisposed();
        await _lifecycleGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ThrowIfDisposingOrDisposed();
            if (_started) return;

            _nodeRid = nodeRid;
            _ownerId = Guid.NewGuid().ToString("n");
            _ownerToken = null;
            _ownerCleanedForDrain = false;

            // Register liveness before any row write so readers joining rows
            // against the lease never see this owner's rows as stale on start.
            if (!await RenewOwnerLeaseOnceAsync(cancellationToken).ConfigureAwait(false))
                throw new InvalidOperationException(
                    $"The location runtime could not establish its owner lease: {LastError ?? "unknown store failure"}");

            var heartbeat = new CancellationTokenSource();
            _heartbeatCts = heartbeat;
            _heartbeatLoop = Task.Run(
                () => HeartbeatLoopAsync(heartbeat.Token),
                CancellationToken.None);
            _started = true;
        }
        finally
        {
            _lifecycleGate.Release();
        }
    }

    internal async ValueTask StopAsync(CancellationToken cancellationToken = default)
    {
        await _lifecycleGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (!_started) return;

            _started = false;
            var heartbeat = _heartbeatCts;
            var heartbeatLoop = _heartbeatLoop;
            _heartbeatCts = null;
            _heartbeatLoop = null;
            if (heartbeat is not null)
                await heartbeat.CancelAsync().ConfigureAwait(false);

            if (heartbeatLoop is not null)
            {
                try
                {
                    await heartbeatLoop.ConfigureAwait(false);
                }
                catch (OperationCanceledException)
                {
                }
            }

            heartbeat?.Dispose();
            UpdateHealth(static health => health with { Healthy = false });

            // Keep the lease valid while owned rows are removed. Allocated routing-id slots are
            // released by their lifecycle before this method removes the shared owner lease.
            // That order prevents another runtime from taking the same id while an old socket or
            // location row can still be observed.
            try
            {
                if (!_ownerCleanedForDrain)
                {
                    await _locationStore.RemoveAllByOwnerAsync(OwnerId, cancellationToken).ConfigureAwait(false);
                    if (_ownerToken is { } token)
                        _ = await _ownerLeaseStore.ReleaseOwnerLeaseAsync(
                                token,
                                cancellationToken)
                            .ConfigureAwait(false);
                    _ownerToken = null;
                }
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                throw;
            }
            catch (Exception exception)
            {
                // A store outage during shutdown is not fatal: the expired
                // lease makes the leftover rows stale and background cleanup
                // removes them later.
                RecordStoreFailure(exception.Message);
            }
        }
        finally
        {
            _lifecycleGate.Release();
        }
    }

    internal async ValueTask CleanupOwnerForDrainAsync(CancellationToken cancellationToken)
    {
        await _lifecycleGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (!_started || _ownerCleanedForDrain) return;

            // Keep the owner lease valid while rows are removed. If row
            // cleanup fails, the heartbeat continues and the next retry sees
            // the same live owner rather than stale state.
            await _locationStore.RemoveAllByOwnerAsync(OwnerId, cancellationToken)
                .ConfigureAwait(false);

            var heartbeat = _heartbeatCts;
            var heartbeatLoop = _heartbeatLoop;
            _heartbeatCts = null;
            _heartbeatLoop = null;
            if (heartbeat is not null)
                await heartbeat.CancelAsync().ConfigureAwait(false);
            if (heartbeatLoop is not null)
                try
                {
                    await heartbeatLoop.ConfigureAwait(false);
                }
                catch (OperationCanceledException)
                {
                }
            heartbeat?.Dispose();

            try
            {
                if (_ownerToken is { } token)
                    _ = await _ownerLeaseStore.ReleaseOwnerLeaseAsync(
                            token,
                            cancellationToken)
                        .ConfigureAwait(false);
                _ownerToken = null;
            }
            catch
            {
                StartHeartbeatAfterCleanupFailure();
                throw;
            }
            _ownerCleanedForDrain = true;
            UpdateHealth(static health => health with { Healthy = false });
        }
        finally
        {
            _lifecycleGate.Release();
        }
    }

    internal async ValueTask RemoveOwnedRowsBeforeRoutingIdReleaseAsync(
        CancellationToken cancellationToken)
    {
        _ = await _locationStore.RemoveAllByOwnerAsync(OwnerId, cancellationToken).ConfigureAwait(false);
    }

    private void StartHeartbeatAfterCleanupFailure()
    {
        if (!_started || _heartbeatCts is not null) return;
        var heartbeat = new CancellationTokenSource();
        _heartbeatCts = heartbeat;
        _heartbeatLoop = Task.Run(
            () => HeartbeatLoopAsync(heartbeat.Token),
            CancellationToken.None);
    }

    public ValueTask DisposeAsync()
    {
        lock (_disposeStartGate)
        {
            if (_disposeTask is not null) return new ValueTask(_disposeTask);
            Volatile.Write(ref _disposeState, 1);
            return new ValueTask(_disposeTask = DisposeCoreAsync());
        }
    }

    private async Task DisposeCoreAsync()
    {
        try
        {
            await StopAsync().ConfigureAwait(false);
        }
        finally
        {
            Volatile.Write(ref _disposeState, 2);
        }
    }

    private void ThrowIfDisposingOrDisposed()
    {
        ObjectDisposedException.ThrowIf(Volatile.Read(ref _disposeState) != 0, this);
    }

    internal async ValueTask<ZLinkLocationWriteResult> WriteDescriptorAsync(
        ZLinkMeshNodeDescriptor descriptor,
        ZLinkLocationWriteIntent intent,
        CancellationToken cancellationToken = default)
    {
        var stamped = descriptor with
        {
            OwnerId = OwnerToken.OwnerId,
            LeaseGeneration = OwnerToken.LeaseGeneration
        };
        return await ExecuteLocationWriteAsync(
                () => _meshNodeStore.UpdateMeshNodeAsync(stamped, intent, cancellationToken),
                ZLinkLocationKind.MeshNode,
                ZLinkLocationKeyCodec.EncodeMeshNodeKey(
                    new ZLinkMeshNodeDescriptorKey(descriptor.MeshName, descriptor.Rid)),
                // Row content generations are writer-owned core values; the
                // store's generation is a separate owner-token fence, so the
                // event carries the row exactly as written (store UpdatedAt
                // applied).
                result => _events.DescriptorRowUpdatedAsync(
                stamped with { UpdatedAt = result.UpdatedAt },
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
                stamped with { UpdatedAt = result.UpdatedAt },
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
                ZLinkLocationKeyCodec.EncodeActorKey(
                    new ZLinkActorLocationKey(stamped.MeshName, stamped.ActorId)),
                result => _events.ActorRowUpdatedAsync(
                stamped with { UpdatedAt = result.UpdatedAt },
                cancellationToken))
            .ConfigureAwait(false);
    }

    internal async ValueTask<ZLinkLocationWriteResult> RemoveSpotAsync(
        ZLinkSpotLocationKey key,
        ulong generation,
        CancellationToken cancellationToken = default)
    {
        return await ExecuteRemoveAsync(
                () => _spotStore.RemoveSpotAsync(
                    key,
                    new ZLinkLocationOwnerToken(
                        OwnerId,
                        checked((long)generation)),
                    cancellationToken),
                ZLinkLocationKind.Spot,
                ZLinkLocationKeyCodec.EncodeSpotKey(key),
                () => _events.SpotRowRemovedAsync(key, generation, cancellationToken))
            .ConfigureAwait(false);
    }

    internal async ValueTask<ZLinkLocationWriteResult> RemoveActorAsync(
        ZLinkActorLocationKey key,
        ulong generation,
        CancellationToken cancellationToken = default)
    {
        return await ExecuteRemoveAsync(
                () => _actorStore.RemoveActorAsync(
                    key,
                    new ZLinkLocationOwnerToken(
                        OwnerId,
                        checked((long)generation)),
                    cancellationToken),
                ZLinkLocationKind.Actor,
                ZLinkLocationKeyCodec.EncodeActorKey(key),
                () => _events.ActorRowRemovedAsync(key, cancellationToken))
            .ConfigureAwait(false);
    }

    internal async ValueTask<ZLinkLocationWriteResult> RemoveDescriptorAsync(
        ZLinkMeshNodeDescriptorKey key,
        ulong generation,
        CancellationToken cancellationToken = default)
    {
        return await ExecuteRemoveAsync(
                () => _meshNodeStore.RemoveMeshNodeAsync(
                    key,
                    new ZLinkLocationOwnerToken(
                        OwnerId,
                        checked((long)generation)),
                    cancellationToken),
                ZLinkLocationKind.MeshNode,
                ZLinkLocationKeyCodec.EncodeMeshNodeKey(key),
                () => _events.DescriptorRowRemovedAsync(key, cancellationToken))
            .ConfigureAwait(false);
    }

    internal async ValueTask<bool> RenewOwnerLeaseOnceAsync(
        CancellationToken cancellationToken = default)
    {
        try
        {
            using var deadline = new CancellationTokenSource(_options.OwnerLeaseRenewTimeout);
            using var operation = CancellationTokenSource.CreateLinkedTokenSource(
                cancellationToken,
                deadline.Token);
            ZLinkOwnerLeaseRenewResult.Renewed result;
            if (_ownerToken is null)
            {
                var claim = await _ownerLeaseStore.ClaimOwnerLeaseAsync(
                        OwnerId,
                        _options.OwnerLeaseTtl,
                        operation.Token)
                    .AsTask()
                    .WaitAsync(deadline.Token)
                    .ConfigureAwait(false);
                if (claim is not ZLinkOwnerLeaseClaimResult.Claimed claimed)
                    throw new InvalidOperationException(
                        $"Owner lease claim failed with '{claim.GetType().Name}'.");
                _ownerToken = claimed.Token;
                result = new ZLinkOwnerLeaseRenewResult.Renewed(
                    claimed.LeaseExpiresAt,
                    claimed.StoreNow);
            }
            else
            {
                var renewal = await _ownerLeaseStore.RenewOwnerLeaseAsync(
                        _ownerToken.Value,
                        _options.OwnerLeaseTtl,
                        operation.Token)
                    .AsTask()
                    .WaitAsync(deadline.Token)
                    .ConfigureAwait(false);
                if (renewal is not ZLinkOwnerLeaseRenewResult.Renewed renewed)
                    throw new InvalidOperationException(
                        "The owner lease token became stale.");
                result = renewed;
            }
            UpdateHealth(
                health => health with
                {
                    Healthy = true,
                    RenewedAt = result.StoreNow,
                    LeaseError = null
                });

            OwnerLeaseRenewed?.Invoke(
                new ZLinkOwnerLeaseRenewal(
                    result.LeaseExpiresAt,
                    result.StoreNow));

            return true;
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch (OperationCanceledException)
        {
            RecordLeaseFailure(
                $"Owner lease renewal timeout after {_options.OwnerLeaseRenewTimeout}.");
            OwnerLeaseRenewalFailed?.Invoke();
            return false;
        }
        catch (Exception exception)
        {
            // Fail-static: record the failure and retry on the next tick.
            // Existing rows stay valid until the lease actually expires.
            RecordLeaseFailure(exception.Message);
            OwnerLeaseRenewalFailed?.Invoke();
            return false;
        }
    }

    private async Task HeartbeatLoopAsync(CancellationToken cancellationToken)
    {
        var intervalTicks = (long)(_options.HeartbeatInterval.TotalSeconds * _time.TimestampFrequency);
        var scheduledRenew = _time.GetTimestamp() + intervalTicks;
        while (!cancellationToken.IsCancellationRequested)
        {
            var remainingTicks = scheduledRenew - _time.GetTimestamp();
            try
            {
                if (remainingTicks > 0)
                    await Task.Delay(
                            TimeSpan.FromSeconds(remainingTicks / (double)_time.TimestampFrequency),
                            _time,
                            cancellationToken)
                        .ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                return;
            }

            ZLinkRuntimeMetrics.RecordOwnerLeaseRenewAttempt(_time, scheduledRenew);
            await RenewOwnerLeaseOnceAsync(cancellationToken).ConfigureAwait(false);
            scheduledRenew += intervalTicks;
        }
    }

    private async ValueTask<ZLinkLocationWriteResult> GuardStoreWriteAsync(
        Func<ValueTask<ZLinkLocationWriteResult>> write)
    {
        try
        {
            var result = await write().ConfigureAwait(false);
            UpdateHealth(static health => health with { StoreError = null });
            return result;
        }
        catch (Exception exception)
        {
            RecordStoreFailure(exception.Message);
            throw;
        }
    }

    private async ValueTask<ZLinkLocationWriteResult> ExecuteRemoveAsync(
        Func<ValueTask<ZLinkLocationWriteStatus>> remove,
        ZLinkLocationKind kind,
        string canonicalKey,
        Func<ValueTask> emitStored)
    {
        ZLinkLocationWriteStatus status;
        try
        {
            status = await remove().ConfigureAwait(false);
            UpdateHealth(static health => health with { StoreError = null });
        }
        catch (Exception exception)
        {
            RecordStoreFailure(exception.Message);
            throw;
        }

        var result = new ZLinkLocationWriteResult(status, 0, default);
        NotifyIfStale(result, kind, canonicalKey);
        if (status == ZLinkLocationWriteStatus.Stored)
        {
            await emitStored().ConfigureAwait(false);
        }

        return result;
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
        if (result.Status is ZLinkLocationWriteStatus.IgnoredStale
            or ZLinkLocationWriteStatus.RejectedConflict)
            ZLinkRuntimeMetrics.RecordLocationWriteConflict();
    }

    private void RecordLeaseFailure(string message)
    {
        ZLinkRuntimeMetrics.RecordOwnerLeaseRenewFailure();
        UpdateHealth(health => health with { Healthy = false, LeaseError = message });
    }

    private void RecordStoreFailure(string message)
    {
        ZLinkRuntimeMetrics.RecordLocationStoreError();
        UpdateHealth(health => health with { StoreError = message });
    }

    private void UpdateHealth(Func<ZLinkLocationRuntimeHealth, ZLinkLocationRuntimeHealth> update)
    {
        while (true)
        {
            var current = Volatile.Read(ref _health);
            var next = update(current);
            if (ReferenceEquals(Interlocked.CompareExchange(ref _health, next, current), current))
                return;
        }
    }
}

internal sealed record ZLinkLocationRuntimeHealth(
    bool Healthy,
    DateTimeOffset? RenewedAt,
    string? LeaseError,
    string? StoreError)
{
    public string? LastError => StoreError ?? LeaseError;
}
