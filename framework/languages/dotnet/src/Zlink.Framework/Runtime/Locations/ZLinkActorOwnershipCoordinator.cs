namespace Zlink.Framework.Runtime.Locations;

internal enum ZLinkActorClaimStatus
{
    Claimed = 1,
    AlreadyOwned = 2,
    Conflict = 3,
    StoreFailure = 4
}

internal readonly record struct ZLinkActorClaimResult(
    ZLinkActorClaimStatus Status,
    ZLinkActorLocation? Existing);

internal readonly record struct ZLinkActorClaimActivation<TActor>(
    TActor? Activated,
    ZLinkActorLocation? ExistingLocation)
    where TActor : class;

internal sealed class ZLinkActorOwnershipCoordinator(
    ZLinkLocationRuntime runtime,
    ZLinkStoreLocationResolvers resolver) : IZLinkActorLocationLifecycle, IAsyncDisposable
{
    private readonly object _gate = new();
    private readonly object _disposeStartGate = new();
    private readonly SemaphoreSlim _backgroundDrainGate = new(1, 1);
    private readonly Dictionary<string, TrackedActor> _actors = new(StringComparer.Ordinal);
    private readonly HashSet<TrackedActor> _trackedActors = [];
    private CancellationTokenSource _reconciliationStop = new();
    private bool _backgroundStopping;
    private int _disposed;
    private Task? _disposeTask;

    public async ValueTask<ZLinkActorClaimActivation<TActor>> ExecuteActorClaimThenActivateAsync<TActor>(
        string actorType,
        string actorId,
        RoutingId nodeRid,
        Func<CancellationToken, ValueTask>? deactivate,
        Func<CancellationToken, ValueTask<TActor>> activate,
        CancellationToken cancellationToken,
        ZLinkActorClaimMode claimMode = ZLinkActorClaimMode.NewOwner)
        where TActor : class
    {
        var claim = await ClaimActorCoreAsync(actorType, actorId, nodeRid, deactivate, claimMode, cancellationToken)
            .ConfigureAwait(false);
        switch (claim.Status)
        {
            case ZLinkActorClaimStatus.AlreadyOwned:
                return new ZLinkActorClaimActivation<TActor>(null, null);

            case ZLinkActorClaimStatus.Conflict:
                return new ZLinkActorClaimActivation<TActor>(null, claim.Existing);

            case ZLinkActorClaimStatus.StoreFailure:
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorCreateFailed,
                    $"Actor '{actorId}' cannot be created because the location store is unavailable.");
        }

        try
        {
            var activated = await activate(cancellationToken).ConfigureAwait(false);
            return new ZLinkActorClaimActivation<TActor>(activated, null);
        }
        catch (Exception activationFailure)
        {
            // The claim preceded the failed activation; without a rollback
            // the key would stay owned by an instance that never existed.
            MarkActivationFailed(actorId);
            try
            {
                await ReleaseActorAsync(actorId, CancellationToken.None).ConfigureAwait(false);
            }
            catch (Exception releaseFailure)
            {
                StartActivationFailureReconciliation(actorId);
                throw new AggregateException(activationFailure, releaseFailure);
            }
            throw;
        }
    }

    private async ValueTask<ZLinkActorClaimResult> ClaimActorCoreAsync(
        string actorType,
        string actorId,
        RoutingId nodeRid,
        Func<CancellationToken, ValueTask>? deactivate,
        ZLinkActorClaimMode claimMode,
        CancellationToken cancellationToken)
    {
        var canonical = ZLinkLocationKeyCodec.EncodeActorKey(
            new ZLinkActorLocationKey(actorId));
        while (true)
        {
            var retryFailedActivation = false;
            lock (_gate)
            {
                if (_actors.TryGetValue(canonical, out var tracked))
                {
                    if (!tracked.ActivationFailed)
                        return new ZLinkActorClaimResult(ZLinkActorClaimStatus.AlreadyOwned, null);
                    retryFailedActivation = true;
                }
            }

            if (!retryFailedActivation) break;
            try
            {
                await ReleaseActorAsync(actorId, cancellationToken).ConfigureAwait(false);
            }
            catch
            {
                return new ZLinkActorClaimResult(ZLinkActorClaimStatus.StoreFailure, null);
            }
        }

        var row = new ZLinkActorLocation(
            actorId,
            ActorType: actorType,
            ActorRef: null,
            nodeRid,
            LocationKind: ZLinkSpotKind.Entry,
            SpotMeshName: string.Empty,
            SpotRid: null,
            OwnerId: string.Empty,
            Generation: 0,
            UpdatedAt: default);
        ZLinkLocationWriteResult result;
        try
        {
            result = await runtime.WriteActorAsync(row, ZLinkLocationWriteIntent.NewClaim, cancellationToken)
                .ConfigureAwait(false);
        }
        catch
        {
            return new ZLinkActorClaimResult(ZLinkActorClaimStatus.StoreFailure, null);
        }
        if (result.Status == ZLinkLocationWriteStatus.RejectedConflict
            && claimMode == ZLinkActorClaimMode.TakeoverExistingOwner)
        {
            // Hosting handoff: the live row belongs to the previous host,
            // which deactivates its instance as part of the move. Takeover
            // is the fencing path that lets the new host claim anyway.
            try
            {
                result = await runtime.WriteActorAsync(row, ZLinkLocationWriteIntent.Takeover, cancellationToken)
                    .ConfigureAwait(false);
            }
            catch
            {
                return new ZLinkActorClaimResult(ZLinkActorClaimStatus.StoreFailure, null);
            }
        }

        switch (result.Status)
        {
            case ZLinkLocationWriteStatus.Stored:
                lock (_gate)
                {
                    var tracked = new TrackedActor(
                        row with { Generation = result.Generation },
                        deactivate);
                    _actors[canonical] = tracked;
                    _trackedActors.Add(tracked);
                }

                return new ZLinkActorClaimResult(ZLinkActorClaimStatus.Claimed, null);

            case ZLinkLocationWriteStatus.RejectedConflict:
            {
                var existing = await ResolveExistingActorAsync(actorId, cancellationToken)
                    .ConfigureAwait(false);
                return new ZLinkActorClaimResult(ZLinkActorClaimStatus.Conflict, existing);
            }

            default:
                return new ZLinkActorClaimResult(ZLinkActorClaimStatus.StoreFailure, null);
        }
    }

    public async ValueTask PublishActorRefAsync(
        string actorId,
        ActorRef actorRef,
        CancellationToken cancellationToken = default)
    {
        await RenewOwnedActorAsync(
                actorId,
                row => row with { ActorRef = actorRef },
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask NotifyActorJoinedSpotAsync(
        string actorId,
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
    {
        await RenewOwnedActorAsync(
                actorId,
                row => row with
                {
                    LocationKind = ZLinkSpotKind.User,
                    SpotRid = spotRid
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask CommitTransferredActorLocationAsync(
        string actorId,
        ActorRef actorRef,
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
    {
        await RenewOwnedActorAsync(
                actorId,
                row => row with
                {
                    ActorRef = actorRef,
                    LocationKind = ZLinkSpotKind.User,
                    SpotRid = spotRid
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask CommitTransferredActorEntryLocationAsync(
        string actorId,
        ActorRef actorRef,
        RoutingId targetNodeRid,
        CancellationToken cancellationToken = default)
    {
        await RenewOwnedActorAsync(
                actorId,
                row => row with
                {
                    ActorRef = actorRef,
                    NodeRid = targetNodeRid,
                    LocationKind = ZLinkSpotKind.Entry,
                    SpotRid = null
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask NotifyActorMovedToEntrySpotAsync(
        string actorId,
        RoutingId targetNodeRid,
        CancellationToken cancellationToken = default)
    {
        await RenewOwnedActorAsync(
                actorId,
                row => row with
                {
                    NodeRid = targetNodeRid,
                    LocationKind = ZLinkSpotKind.Entry,
                    SpotRid = null
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask NotifyActorLeftSpotAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        await RenewOwnedActorAsync(
                actorId,
                row => row with
                {
                    LocationKind = ZLinkSpotKind.Entry,
                    SpotRid = null
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask ReleaseActorAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var key = new ZLinkActorLocationKey(actorId);
        var canonical = ZLinkLocationKeyCodec.EncodeActorKey(key);
        TrackedActor tracked;
        TaskCompletionSource? releaseCompletion = null;
        Task releaseTask;
        lock (_gate)
        {
            if (!_actors.TryGetValue(canonical, out tracked!)) return;
            if (tracked.ReleaseTask is null)
            {
                releaseCompletion = new TaskCompletionSource(
                    TaskCreationOptions.RunContinuationsAsynchronously);
                tracked.ReleaseTask = releaseCompletion.Task;
            }

            releaseTask = tracked.ReleaseTask;
        }

        if (releaseCompletion is not null)
        {
            try
            {
                await ReleaseTrackedActorAsync(key, canonical, tracked).ConfigureAwait(false);
                releaseCompletion.TrySetResult();
            }
            catch (Exception exception)
            {
                lock (_gate)
                {
                    if (_actors.TryGetValue(canonical, out var current)
                        && ReferenceEquals(current, tracked)
                        && ReferenceEquals(current.ReleaseTask, releaseTask))
                        current.ReleaseTask = null;
                }

                releaseCompletion.TrySetException(exception);
            }
        }

        await releaseTask.WaitAsync(cancellationToken).ConfigureAwait(false);
    }

    internal bool OwnsActor(string actorId)
    {
        var canonical = ZLinkLocationKeyCodec.EncodeActorKey(new ZLinkActorLocationKey(
            actorId));
        lock (_gate)
        {
            return _actors.ContainsKey(canonical);
        }
    }

    internal void ResetGeneration()
    {
        lock (_gate) _actors.Clear();
    }

    private void MarkActivationFailed(string actorId)
    {
        var canonical = ZLinkLocationKeyCodec.EncodeActorKey(new ZLinkActorLocationKey(actorId));
        lock (_gate)
        {
            if (_actors.TryGetValue(canonical, out var tracked))
                tracked.ActivationFailed = true;
        }
    }

    private void StartActivationFailureReconciliation(string actorId)
    {
        var canonical = ZLinkLocationKeyCodec.EncodeActorKey(new ZLinkActorLocationKey(actorId));
        TrackedActor? tracked;
        lock (_gate)
        {
            if (!_actors.TryGetValue(canonical, out tracked)
                || Interlocked.Exchange(ref tracked.ReconciliationStarted, 1) != 0)
                return;
            if (_disposed != 0 || _backgroundStopping)
            {
                Interlocked.Exchange(ref tracked.ReconciliationStarted, 0);
                return;
            }
            tracked.ReconciliationTask = ReconcileActivationFailureAsync(
                actorId,
                canonical,
                tracked,
                _reconciliationStop.Token);
        }
    }

    private async Task ReconcileActivationFailureAsync(
        string actorId,
        string canonical,
        TrackedActor tracked,
        CancellationToken stopToken)
    {
        try
        {
            await ZLinkReconciliationRunner.RunAsync(
                    token => ReleaseActorAsync(actorId, token),
                    exception => ZLinkFrameworkDebugLog.SpotDiscovery(
                        $"failed actor activation claim release retry for '{actorId}': {exception.Message}"),
                    stopToken)
                .ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (stopToken.IsCancellationRequested)
        {
        }
        finally
        {
            lock (_gate)
            {
                if (_actors.TryGetValue(canonical, out var current)
                    && ReferenceEquals(current, tracked))
                    Interlocked.Exchange(ref tracked.ReconciliationStarted, 0);
            }
        }
    }

    public ValueTask DisposeAsync()
    {
        lock (_disposeStartGate)
            return new ValueTask(_disposeTask ??= DisposeCoreAsync());
    }

    private async Task DisposeCoreAsync()
    {
        Interlocked.Exchange(ref _disposed, 1);
        _reconciliationStop.Cancel();
        await DrainBackgroundWorkCoreAsync().ConfigureAwait(false);

        TrackedActor[] trackedActors;
        lock (_gate)
        {
            trackedActors = _trackedActors.ToArray();
            _actors.Clear();
            _trackedActors.Clear();
        }

        foreach (var tracked in trackedActors)
        {
            await tracked.WriteGate.WaitAsync(CancellationToken.None).ConfigureAwait(false);
            tracked.WriteGate.Dispose();
        }

        _reconciliationStop.Dispose();
        _backgroundDrainGate.Dispose();
    }

    internal ValueTask PauseBackgroundWorkAsync()
        => DrainBackgroundWorkCoreAsync();

    internal void ResumeBackgroundWork()
    {
        CancellationTokenSource? stopped = null;
        lock (_gate)
        {
            if (_disposed != 0 || !_backgroundStopping) return;
            if (_reconciliationStop.IsCancellationRequested)
            {
                stopped = _reconciliationStop;
                _reconciliationStop = new CancellationTokenSource();
            }
            _backgroundStopping = false;
        }
        stopped?.Dispose();
    }

    private async ValueTask DrainBackgroundWorkCoreAsync()
    {
        await _backgroundDrainGate.WaitAsync(CancellationToken.None).ConfigureAwait(false);
        try
        {
            Task[] tasks;
            CancellationTokenSource stop;
            lock (_gate)
            {
                stop = _reconciliationStop;
                _backgroundStopping = true;
                if (!stop.IsCancellationRequested) stop.Cancel();
                tasks = _actors.Values
                    .SelectMany(static actor => new[] { actor.ReconciliationTask, actor.ReleaseTask })
                    .Where(static task => task is not null)
                    .Cast<Task>()
                    .Distinct()
                    .ToArray();
            }

            if (tasks.Length != 0)
                try
                {
                    await Task.WhenAll(tasks).ConfigureAwait(false);
                }
                catch (OperationCanceledException) when (stop.IsCancellationRequested)
                {
                }
                catch (Exception exception)
                {
                    ZLinkFrameworkDebugLog.SpotDiscovery(
                        $"actor ownership background drain failed: {exception.Message}");
                }

            lock (_gate)
            {
                _backgroundStopping = true;
            }
        }
        finally
        {
            _backgroundDrainGate.Release();
        }
    }

    internal Func<CancellationToken, ValueTask>? TakeOwnershipLostDeactivation(string canonicalKey)
    {
        lock (_gate)
        {
            return _actors.Remove(canonicalKey, out var actor) ? actor.Deactivate : null;
        }
    }

    private async ValueTask<ZLinkActorLocation?> ResolveExistingActorAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        try
        {
            return await resolver.ResolveActorRowAsync(
                    new ZLinkActorLocationKey(actorId),
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            ZLinkFrameworkDebugLog.SpotDiscovery(
                $"actor conflict resolve failed for '{actorId}': {exception.Message}");
            return null;
        }
    }

    private async ValueTask RenewOwnedActorAsync(
        string actorId,
        Func<ZLinkActorLocation, ZLinkActorLocation> mutate,
        CancellationToken cancellationToken)
    {
        var canonical = ZLinkLocationKeyCodec.EncodeActorKey(new ZLinkActorLocationKey(
            actorId));
        TrackedActor? tracked;
        lock (_gate)
        {
            if (!_actors.TryGetValue(canonical, out tracked))
            {
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"Actor '{actorId}' is not tracked by this location owner.");
            }

            if (tracked.ReleaseTask is not null)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"Actor '{actorId}' location is being released.");
        }

        await tracked.WriteGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ZLinkActorLocation proposed;
            lock (_gate)
            {
                if (!_actors.TryGetValue(canonical, out var current)
                    || !ReferenceEquals(current, tracked))
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorLocationStale,
                        $"Actor '{actorId}' is no longer tracked by this location owner.");
                if (tracked.ReleaseTask is not null)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"Actor '{actorId}' location is being released.");

                proposed = mutate(tracked.Row);
            }

            // The tracked row describes the last store-confirmed state. A
            // rejected or failed write must not become the base of a later
            // renew.
            var result = await runtime.WriteActorAsync(
                    proposed,
                    ZLinkLocationWriteIntent.Renew,
                    cancellationToken)
                .ConfigureAwait(false);
            if (result.Status != ZLinkLocationWriteStatus.Stored)
                throw new ZLinkFrameworkException(
                    result.Status == ZLinkLocationWriteStatus.IgnoredStale
                        ? ZLinkFrameworkErrorKind.ActorLocationStale
                        : ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"Actor '{actorId}' location update was rejected with '{result.Status}'.");

            lock (_gate)
            {
                if (_actors.TryGetValue(canonical, out var current)
                    && ReferenceEquals(current, tracked))
                    tracked.Row = proposed with { Generation = result.Generation };
            }
        }
        finally
        {
            tracked.WriteGate.Release();
        }
    }

    private async Task ReleaseTrackedActorAsync(
        ZLinkActorLocationKey key,
        string canonical,
        TrackedActor tracked)
    {
        await tracked.WriteGate.WaitAsync(CancellationToken.None).ConfigureAwait(false);
        try
        {
            ZLinkActorLocation row;
            lock (_gate)
            {
                if (!_actors.TryGetValue(canonical, out var current)
                    || !ReferenceEquals(current, tracked))
                    return;
                row = tracked.Row;
            }

            var result = await runtime.RemoveActorAsync(
                    key,
                    row.Generation,
                    CancellationToken.None)
                .ConfigureAwait(false);
            if (result.Status is not (ZLinkLocationWriteStatus.Stored or ZLinkLocationWriteStatus.IgnoredStale))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"Actor '{key.ActorId}' location release was rejected with '{result.Status}'.");

            lock (_gate)
            {
                if (_actors.TryGetValue(canonical, out var current)
                    && ReferenceEquals(current, tracked))
                    _actors.Remove(canonical);
            }
        }
        finally
        {
            tracked.WriteGate.Release();
        }
    }

    private sealed class TrackedActor(
        ZLinkActorLocation row,
        Func<CancellationToken, ValueTask>? deactivate)
    {
        public ZLinkActorLocation Row { get; set; } = row;

        public Func<CancellationToken, ValueTask>? Deactivate { get; } = deactivate;

        public SemaphoreSlim WriteGate { get; } = new(1, 1);

        public Task? ReleaseTask { get; set; }

        public Task? ReconciliationTask { get; set; }

        public bool ActivationFailed { get; set; }

        public int ReconciliationStarted;
    }
}
