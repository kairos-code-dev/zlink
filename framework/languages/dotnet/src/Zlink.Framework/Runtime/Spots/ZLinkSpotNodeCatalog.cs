namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotNodeCatalog(
    IServiceProvider services,
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration frameworkRegistration,
    ZLinkSpotNodeRegistration registration,
    IZLinkBackendSpotNode node,
    string spotChannelName,
    ZLinkLocationLifecycle? lifecycle) : IAsyncDisposable
{
    private readonly object _disposeGate = new();
    private Task? _disposeTask;
    private readonly ZLinkSpotActivationFactory _activationFactory = new(
        services,
        runtime,
        frameworkRegistration,
        registration,
        node,
        spotChannelName);

    private readonly object _gate = new();
    private readonly Dictionary<string, TaskCompletionSource<bool>> _closing = [];
    private readonly Dictionary<string, PendingSpotCreation> _pending = [];
    private readonly Dictionary<string, ZLinkSpotActivation> _spots = [];
    private TaskCompletionSource? _creationsDrained;
    private int _activeCreations;
    private bool _closed;

    public IReadOnlyCollection<ZLinkSpotActivation> Spots => SnapshotActivations();

    internal async ValueTask<bool> TryDrainAsync(CancellationToken cancellationToken)
    {
        var activations = SnapshotActivations();
        foreach (var activation in activations)
            await CloseAsync(activation.SpotId, cancellationToken).ConfigureAwait(false);

        lock (_gate) return _spots.Count == 0;
    }

    internal void RequestStop()
    {
        foreach (var activation in SnapshotActivations()) activation.RequestStop();
    }

    internal void CancelActiveOperations()
    {
        foreach (var activation in SnapshotActivations()) activation.CancelActiveOperations();
    }

    internal async ValueTask CloseLifecycleAsync()
    {
        var activations = SnapshotActivations();
        List<Exception>? failures = null;
        foreach (var activation in activations)
        {
            var spotId = activation.SpotId;
            TaskCompletionSource<bool> transaction;
            bool ownsTransaction;
            lock (_gate)
            {
                if (!_spots.ContainsKey(spotId)) continue;
                if (_closing.TryGetValue(spotId, out transaction!))
                {
                    ownsTransaction = false;
                }
                else
                {
                    transaction = new TaskCompletionSource<bool>(
                        TaskCreationOptions.RunContinuationsAsynchronously);
                    _closing.Add(spotId, transaction);
                    ownsTransaction = true;
                }
            }

            await CaptureAsync(async () =>
                {
                    if (ownsTransaction)
                        _ = await ExecuteCloseTransactionAsync(
                                spotId,
                                activation,
                                transaction)
                            .ConfigureAwait(false);
                    else
                        _ = await transaction.Task.ConfigureAwait(false);
                })
                .ConfigureAwait(false);

            bool stillTracked;
            lock (_gate) stillTracked = _spots.ContainsKey(spotId);
            if (stillTracked)
                await CaptureAsync(() => ForceCloseForShutdownAsync(activation)).ConfigureAwait(false);
        }

        if (failures is { Count: 1 })
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failures[0]).Throw();
        if (failures is { Count: > 1 }) throw new AggregateException(failures);
        return;

        async ValueTask CaptureAsync(Func<ValueTask> cleanup)
        {
            try
            {
                await cleanup().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                (failures ??= []).Add(exception);
            }
        }
    }

    public ValueTask DisposeAsync()
    {
        lock (_disposeGate)
        {
            if (_disposeTask is not null) return new ValueTask(_disposeTask);

            Task creationsDrained;
            lock (_gate)
            {
                _closed = true;
                creationsDrained = _activeCreations == 0
                    ? Task.CompletedTask
                    : (_creationsDrained ??= new TaskCompletionSource(
                        TaskCreationOptions.RunContinuationsAsynchronously)).Task;
            }

            return new ValueTask(_disposeTask = DisposeCoreAsync(creationsDrained));
        }
    }

    private async Task DisposeCoreAsync(Task creationsDrained)
    {
        List<Exception>? failures = null;
        await CaptureAsync(() => new ValueTask(creationsDrained)).ConfigureAwait(false);
        await CaptureAsync(CloseLifecycleAsync).ConfigureAwait(false);

        ZLinkSpotActivation[] activations;
        lock (_gate)
        {
            activations = _spots.Values.ToArray();
        }

        foreach (var activation in activations)
        {
            var spotId = activation.SpotId;
            await CaptureAsync(activation.DisposeAsync).ConfigureAwait(false);
            lock (_gate)
            {
                _spots.Remove(spotId);
                _closing.Remove(spotId);
            }
        }

        if (failures is { Count: 1 })
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failures[0]).Throw();
        if (failures is { Count: > 1 }) throw new AggregateException(failures);
        return;

        async ValueTask CaptureAsync(Func<ValueTask> cleanup)
        {
            try
            {
                await cleanup().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                (failures ??= []).Add(exception);
            }
        }
    }

    public async ValueTask<ZLinkSpotCreateResult> CreateAsync(
        Type spotType,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            EnsureSpotTypeRegisteredLocked(spotType);
            BeginCreationLocked();
        }

        IZLinkBackendSpot? nativeSpot = null;
        ZLinkSpotActivation? activation = null;
        try
        {
            var spotId = Guid.NewGuid().ToString("D");
            nativeSpot = node.GetOrCreateSpot(spotId, out var created);
            if (!created)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotIdConflict,
                    $"Generated User Spot ID '{spotId}' is already active.");
            var creation = await _activationFactory.CreateAsync(
                spotType,
                nativeSpot,
                spotId,
                request,
                cancellationToken);
            activation = creation.Activation;

            if (!creation.Response.Accepted)
            {
                var rejected = new ZLinkSpotCreateResult(
                    Reference(activation),
                    ZLinkSpotCreateState.Rejected,
                    creation.Response.Reply);
                await DisposeFailedCreationAsync(activation);
                return rejected;
            }

            cancellationToken.ThrowIfCancellationRequested();
            await ClaimSpotLocationAsync(activation, spotType, cancellationToken)
                .ConfigureAwait(false);
            lock (_gate)
            {
                _spots.Add(activation.SpotId, activation);
            }
            ZLinkRuntimeMetrics.RecordSpotCreated("user");

            return new ZLinkSpotCreateResult(
                Reference(activation),
                ZLinkSpotCreateState.Created,
                creation.Response.Reply);
        }
        catch (Exception error)
        {
            RemoveActivation(activation);
            var failures = new ZLinkFailureCollector(WrapSpotCreateFailed(spotType, error));
            if (activation is not null)
                await failures.CaptureAsync(activation.DisposeAsync).ConfigureAwait(false);
            failures.ThrowIfAny();
            throw new InvalidOperationException("Unreachable after creation cleanup failure propagation.");
        }
        finally
        {
            EndCreation();
        }
    }

    public async ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync(
        Type spotType,
        string requestedSpotId,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        PendingSpotCreation pending;
        var owner = false;
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            EnsureSpotTypeRegisteredLocked(spotType);
            EnsureCreationAdmissionOpenLocked();

            if (_spots.TryGetValue(requestedSpotId, out var existing))
            {
                ThrowIfSpotTypeMismatch(existing.Spot.GetType(), spotType, requestedSpotId);
                return new ZLinkSpotCreateResult(
                    Reference(existing),
                    ZLinkSpotCreateState.Existing,
                    null);
            }

            if (_pending.TryGetValue(requestedSpotId, out pending!))
            {
                ThrowIfSpotTypeMismatch(pending.SpotType, spotType, requestedSpotId);
            }
            else
            {
                BeginCreationLocked();
                pending = new PendingSpotCreation(spotType);
                _pending.Add(requestedSpotId, pending);
                owner = true;
            }
        }

        if (owner)
            _ = CompletePendingCreationAsync(
                spotType,
                requestedSpotId,
                request,
                pending,
                runtime.ShutdownToken,
                claimLegacyLocation: true);

        var result = await pending.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
        return !owner && result.State == ZLinkSpotCreateState.Created
            ? result with { State = ZLinkSpotCreateState.Existing }
            : result;
    }

    private async ValueTask CompletePendingCreationAsync(
        Type spotType,
        string requestedSpotId,
        ZLinkMessage request,
        PendingSpotCreation pending,
        CancellationToken cancellationToken,
        bool claimLegacyLocation)
    {
        IZLinkBackendSpot? nativeSpot = null;
        ZLinkSpotActivation? activation = null;
        var factoryOwnsNativeSpot = false;
        try
        {
            nativeSpot = node.GetOrCreateSpot(requestedSpotId, out var created);
            if (!created)
            {
                var existingNativeSpot = nativeSpot;
                nativeSpot = null;
                await existingNativeSpot.DisposeAsync();
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotCreateFailed,
                    $"SPOT routing id '{requestedSpotId}' already exists in core but no framework SPOT is registered.");
            }

            factoryOwnsNativeSpot = true;
            var creation = await _activationFactory.CreateAsync(
                spotType,
                nativeSpot,
                requestedSpotId,
                request,
                cancellationToken);
            activation = creation.Activation;

            if (!creation.Response.Accepted)
            {
                var rejected = new ZLinkSpotCreateResult(
                    Reference(activation),
                    ZLinkSpotCreateState.Rejected,
                    creation.Response.Reply);
                await DisposeFailedCreationAsync(activation).ConfigureAwait(false);
                lock (_gate)
                {
                    _pending.Remove(requestedSpotId);
                    pending.Complete(rejected);
                }
                return;
            }

            cancellationToken.ThrowIfCancellationRequested();
            if (claimLegacyLocation)
                await ClaimSpotLocationAsync(activation, spotType, cancellationToken)
                    .ConfigureAwait(false);

            var result = new ZLinkSpotCreateResult(
                Reference(activation),
                ZLinkSpotCreateState.Created,
                creation.Response.Reply);
            lock (_gate)
            {
                _pending.Remove(requestedSpotId);
                _spots.Add(activation.SpotId, activation);
                pending.Complete(result);
            }
            ZLinkRuntimeMetrics.RecordSpotCreated("user");
        }
        catch (Exception error)
        {
            ZLinkFrameworkDebugLog.SpotDiscovery(
                $"SPOT '{requestedSpotId}' creation failed on node '{node.RoutingId}': {error}");
            var wrapped = WrapSpotCreateFailed(spotType, error);
            lock (_gate)
            {
                RemoveActivationLocked(activation);
            }

            var failures = new ZLinkFailureCollector(wrapped);
            if (activation is not null)
                await failures.CaptureAsync(activation.DisposeAsync).ConfigureAwait(false);
            else if (!factoryOwnsNativeSpot && nativeSpot is not null)
                await failures.CaptureAsync(nativeSpot.DisposeAsync).ConfigureAwait(false);
            var finalFailure = failures.BuildException()!;
            lock (_gate)
            {
                _pending.Remove(requestedSpotId);
                pending.Fail(finalFailure);
            }
        }
        finally
        {
            EndCreation();
        }
    }

    internal async ValueTask<PreparedReservedSpot> PrepareReservedAsync(
        Type spotType,
        string requestedSpotId,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        var pending = new PendingSpotCreation(spotType);
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            EnsureSpotTypeRegisteredLocked(spotType);
            EnsureCreationAdmissionOpenLocked();
            if (_spots.TryGetValue(requestedSpotId, out var existing))
            {
                ThrowIfSpotTypeMismatch(existing.Spot.GetType(), spotType, requestedSpotId);
                return new PreparedReservedSpot(existing, true, null);
            }
            if (_pending.ContainsKey(requestedSpotId))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotMoving,
                    $"SPOT '{requestedSpotId}' is already being materialized.",
                    true);

            BeginCreationLocked();
            _pending.Add(requestedSpotId, pending);
        }

        IZLinkBackendSpot? nativeSpot = null;
        ZLinkSpotActivation? activation = null;
        try
        {
            nativeSpot = node.GetOrCreateSpot(requestedSpotId, out var created);
            if (!created)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotMoving,
                    $"SPOT '{requestedSpotId}' is already materialized.");
            var creation = await _activationFactory.CreateAsync(
                    spotType, nativeSpot, requestedSpotId, request, cancellationToken)
                .ConfigureAwait(false);
            activation = creation.Activation;
            lock (_gate) _pending.Remove(requestedSpotId);
            EndCreation();
            return new PreparedReservedSpot(
                activation,
                false,
                creation.Response);
        }
        catch
        {
            lock (_gate) _pending.Remove(requestedSpotId);
            if (activation is not null)
                await activation.DisposeAsync().ConfigureAwait(false);
            else if (nativeSpot is not null)
                await nativeSpot.DisposeAsync().ConfigureAwait(false);
            EndCreation();
            throw;
        }
    }

    internal void PublishReserved(PreparedReservedSpot prepared)
    {
        if (prepared.Existing) return;
        lock (_gate)
        {
            if (_spots.ContainsKey(prepared.Activation.SpotId))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotMoving,
                    $"SPOT '{prepared.Activation.SpotId}' became visible before publication.");
            _spots.Add(prepared.Activation.SpotId, prepared.Activation);
        }
        ZLinkRuntimeMetrics.RecordSpotCreated("user");
    }

    internal async ValueTask DiscardReservedAsync(PreparedReservedSpot prepared)
    {
        if (!prepared.Existing)
            await prepared.Activation.DisposeAsync().ConfigureAwait(false);
    }

    internal bool HasActiveActors(string spotId)
    {
        lock (_gate)
            return _spots.TryGetValue(spotId, out var activation)
                   && activation.JoinedActorCount > 0;
    }

    internal ReservedSpotCloseReadiness CloseReadiness(string spotId)
    {
        lock (_gate)
        {
            if (!_spots.TryGetValue(spotId, out var activation))
                return ReservedSpotCloseReadiness.LocalMissing;
            if (_closing.ContainsKey(spotId))
                return ReservedSpotCloseReadiness.Closing;
            return activation.JoinedActorCount == 0
                ? ReservedSpotCloseReadiness.Ready
                : ReservedSpotCloseReadiness.HasActors;
        }
    }

    private static SpotRef Reference(ZLinkSpotActivation activation) =>
        new(
            activation.SpotId,
            activation.NativeSpot.LifecycleGeneration,
            activation.SpotNodeName,
            activation.NodeRid);

    public ValueTask<ZLinkSpotInfo?> GetAsync(
        string spotId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            ZLinkSpotInfo? result = _spots.TryGetValue(spotId, out var activation)
                ? new ZLinkSpotInfo(activation.SpotId)
                : null;
            return ValueTask.FromResult(result);
        }
    }

    public ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            IReadOnlyList<ZLinkSpotInfo> result = _spots.Values
                .Select(static activation => new ZLinkSpotInfo(activation.SpotId))
                .OrderBy(static item => item.SpotId, StringComparer.Ordinal)
                .ToArray();
            return ValueTask.FromResult(result);
        }
    }

    public async ValueTask<bool> CloseAsync(
        string spotId,
        CancellationToken cancellationToken)
    {
        ZLinkSpotActivation? activation;
        TaskCompletionSource<bool>? transaction;
        var ownsTransaction = false;
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (_closing.TryGetValue(spotId, out transaction))
            {
                activation = null;
            }
            else
            {
                if (!_spots.TryGetValue(spotId, out activation)) return false;

                transaction = new TaskCompletionSource<bool>(
                    TaskCreationOptions.RunContinuationsAsynchronously);
                _closing.Add(spotId, transaction);
                ownsTransaction = true;
            }
        }

        if (!ownsTransaction)
            return await transaction!.Task.WaitAsync(cancellationToken).ConfigureAwait(false);

        if (ReferenceEquals(ZLinkSpotAmbientContext.CurrentOrDefault, activation))
        {
            if (!runtime.TryRunDetached(
                    "spot-close-after-current-turn",
                    async _ =>
                    {
                        await ExecuteCloseTransactionAsync(spotId, activation!, transaction!)
                            .ConfigureAwait(false);
                    }))
            {
                lock (_gate) _closing.Remove(spotId);
                transaction!.TrySetException(new InvalidOperationException(
                    $"SPOT '{spotId}' close could not be scheduled in the current runtime generation."));
                return false;
            }

            return true;
        }

        return await ExecuteCloseTransactionAsync(spotId, activation!, transaction!)
            .ConfigureAwait(false);
    }

    /// <summary>Spot lifecycle write (draft 15.1): a created user spot
    /// claims its location row; the store-issued generation stays with the
    /// tracked entry so stop/destroy can present the owner token. A claim
    /// that cannot be stored fails the creation, because an unadvertised
    /// or doubly-claimed spot rid would break single-activation.</summary>
    private async ValueTask ClaimSpotLocationAsync(
        ZLinkSpotActivation activation,
        Type spotType,
        CancellationToken cancellationToken)
    {
        if (lifecycle is null) return;

        var spotId = activation.SpotId;
        var status = await lifecycle.SpotLocations.ClaimAsync(
                spotChannelName,
                spotId,
                activation.NativeSpot.LifecycleGeneration,
                spotType.FullName,
                node.RoutingId,
                node.MeshStatus().LifecycleGeneration,
                ZLinkSpotKind.User,
                deactivate: async ct => _ = await CloseAsync(spotId, ct).ConfigureAwait(false),
                cancellationToken)
            .ConfigureAwait(false);
        if (status == ZLinkLocationWriteStatus.Stored) return;

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.SpotCreateFailed,
            status == ZLinkLocationWriteStatus.RejectedConflict
                ? $"SPOT '{spotId}' location in mesh '{spotChannelName}' is owned by another node."
                : $"SPOT '{spotId}' location claim failed because the location store is unavailable.");
    }

    private async ValueTask ReleaseSpotLocationAsync(string spotId)
    {
        if (lifecycle is not null)
            await lifecycle.SpotLocations.ReleaseAsync(spotChannelName, spotId).ConfigureAwait(false);
    }

    private IReadOnlyCollection<ZLinkSpotActivation> SnapshotActivations()
    {
        lock (_gate)
        {
            return _spots.Values.ToArray();
        }
    }

    private async ValueTask<bool> ExecuteCloseTransactionAsync(
        string spotId,
        ZLinkSpotActivation activation,
        TaskCompletionSource<bool> transaction)
    {
        try
        {
            if (!await activation.TryCloseIfNoActorsAsync(CancellationToken.None)
                    .ConfigureAwait(false))
            {
                lock (_gate) _closing.Remove(spotId);
                transaction.TrySetResult(false);
                return false;
            }

            await activation.DisposeAsync().ConfigureAwait(false);
            await ReleaseSpotLocationAsync(spotId).ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            lock (_gate)
            {
                _spots.Remove(spotId);
                _closing.Remove(spotId);
            }
            transaction.TrySetException(exception);
            throw;
        }

        lock (_gate)
        {
            _spots.Remove(spotId);
            _closing.Remove(spotId);
        }
        ZLinkRuntimeMetrics.RecordSpotClosed("user");

        transaction.TrySetResult(true);
        return true;
    }

    internal static async ValueTask CloseBeforeReleaseAsync(
        Func<ValueTask> closeSpot,
        Func<ValueTask> releaseLocation)
    {
        await closeSpot().ConfigureAwait(false);
        await releaseLocation().ConfigureAwait(false);
    }

    private async ValueTask ForceCloseForShutdownAsync(ZLinkSpotActivation activation)
    {
        var failures = new ZLinkFailureCollector();
        await failures.CaptureAsync(() => activation.CloseAsync(CancellationToken.None)).ConfigureAwait(false);
        await failures.CaptureAsync(() => ReleaseSpotLocationAsync(activation.SpotId)).ConfigureAwait(false);
        await failures.CaptureAsync(activation.DisposeAsync).ConfigureAwait(false);
        lock (_gate)
        {
            _spots.Remove(activation.SpotId);
            _closing.Remove(activation.SpotId);
        }
        ZLinkRuntimeMetrics.RecordSpotClosed("user");
        failures.ThrowIfAny();
    }

    private void RemoveActivation(ZLinkSpotActivation? activation)
    {
        if (activation is null) return;

        lock (_gate)
        {
            RemoveActivationLocked(activation);
        }
    }

    private void RemoveActivationLocked(ZLinkSpotActivation? activation)
    {
        if (activation is not null) _spots.Remove(activation.SpotId);
    }

    private static async ValueTask DisposeFailedCreationAsync(ZLinkSpotActivation activation)
    {
        await activation.DisposeAsync();
    }

    private void EnsureSpotTypeRegisteredLocked(Type spotType)
    {
        if (!registration.SpotFactories.Contains(spotType))
            throw new ZLinkConfigurationException(
                $"SPOT factory '{spotType}' is not registered on node '{registration.SpotNodeName}'.");
    }

    private void BeginCreationLocked()
    {
        EnsureCreationAdmissionOpenLocked();
        _activeCreations++;
    }

    private void EnsureCreationAdmissionOpenLocked()
    {
        ObjectDisposedException.ThrowIf(_closed, this);
    }

    private void EndCreation()
    {
        TaskCompletionSource? drained = null;
        lock (_gate)
        {
            if (--_activeCreations < 0)
                throw new InvalidOperationException("SPOT creation admission count became negative.");
            if (_closed && _activeCreations == 0)
            {
                drained = _creationsDrained;
                _creationsDrained = null;
            }
        }

        drained?.TrySetResult();
    }

    private static void ThrowIfSpotTypeMismatch(
        Type existingSpotType,
        Type requestedSpotType,
        string spotId)
    {
        if (existingSpotType == requestedSpotType) return;

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.SpotTypeMismatch,
            $"SPOT routing id '{spotId}' already belongs to '{existingSpotType}'.");
    }

    private static Exception WrapSpotCreateFailed(
        Type spotType,
        Exception error)
    {
        if (error is OperationCanceledException) return error;
        if (error is ZLinkFrameworkException frameworkError) return frameworkError;

        return new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.SpotCreateFailed,
            $"SPOT '{spotType}' creation failed.",
            innerException: error);
    }

    private sealed class PendingSpotCreation(Type spotType)
    {
        private readonly TaskCompletionSource<ZLinkSpotCreateResult> _completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public Type SpotType { get; } = spotType;

        public Task<ZLinkSpotCreateResult> Task => _completion.Task;

        public void Complete(ZLinkSpotCreateResult result)
        {
            _completion.TrySetResult(result);
        }

        public void Fail(Exception error)
        {
            _completion.TrySetException(error);
        }
    }
}

internal sealed record PreparedReservedSpot(
    ZLinkSpotActivation Activation,
    bool Existing,
    ZLinkSpotCreateResponse? Response);

internal enum ReservedSpotCloseReadiness
{
    Ready,
    HasActors,
    LocalMissing,
    Closing
}
