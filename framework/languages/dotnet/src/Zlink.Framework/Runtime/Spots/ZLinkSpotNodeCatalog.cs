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
    private readonly Dictionary<RoutingId, TaskCompletionSource<bool>> _closing = [];
    private readonly Dictionary<RoutingId, PendingSpotCreation> _pending = [];
    private readonly Dictionary<RoutingId, ZLinkSpotActivation> _spots = [];
    private TaskCompletionSource? _creationsDrained;
    private int _activeCreations;
    private bool _closed;
    private string? _activeDrainMetricPolicy;

    public IReadOnlyCollection<ZLinkSpotActivation> Spots => SnapshotActivations();

    internal void BeginDrain(ZLinkSpotDrainPolicy policy)
    {
        Volatile.Write(ref _activeDrainMetricPolicy, MetricPolicy(policy));
    }

    internal async ValueTask<bool> TryDrainAsync(
        ZLinkSpotDrainPolicy policy,
        CancellationToken cancellationToken)
    {
        BeginDrain(policy);
        if (policy == ZLinkSpotDrainPolicy.ReleaseAndRecreate)
        {
            var activations = SnapshotActivations();
            foreach (var activation in activations)
                await CloseAsync(activation.SpotRid, cancellationToken).ConfigureAwait(false);
        }

        lock (_gate) return _spots.Count == 0;
    }

    private static string MetricPolicy(ZLinkSpotDrainPolicy policy) => policy switch
    {
        ZLinkSpotDrainPolicy.DrainNatural => "drain_natural",
        ZLinkSpotDrainPolicy.ReleaseAndRecreate => "release_and_recreate",
        _ => throw new ArgumentOutOfRangeException(nameof(policy), policy, "Unknown SPOT drain policy.")
    };

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
            var spotRid = activation.SpotRid;
            TaskCompletionSource<bool> transaction;
            bool ownsTransaction;
            lock (_gate)
            {
                if (!_spots.ContainsKey(spotRid)) continue;
                if (_closing.TryGetValue(spotRid, out transaction!))
                {
                    ownsTransaction = false;
                }
                else
                {
                    transaction = new TaskCompletionSource<bool>(
                        TaskCreationOptions.RunContinuationsAsynchronously);
                    _closing.Add(spotRid, transaction);
                    ownsTransaction = true;
                }
            }

            await CaptureAsync(async () =>
                {
                    if (ownsTransaction)
                        _ = await ExecuteCloseTransactionAsync(
                                spotRid,
                                activation,
                                transaction)
                            .ConfigureAwait(false);
                    else
                        _ = await transaction.Task.ConfigureAwait(false);
                })
                .ConfigureAwait(false);

            bool stillTracked;
            lock (_gate) stillTracked = _spots.ContainsKey(spotRid);
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
            var spotRid = activation.SpotRid;
            await CaptureAsync(activation.DisposeAsync).ConfigureAwait(false);
            lock (_gate)
            {
                _spots.Remove(spotRid);
                _closing.Remove(spotRid);
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
            nativeSpot = node.CreateSpot();
            var creation = await _activationFactory.CreateAsync(
                spotType,
                nativeSpot,
                request,
                cancellationToken);
            activation = creation.Activation;

            if (!creation.Response.Accepted)
            {
                var rejected = new ZLinkSpotCreateResult(
                    activation.SpotRid,
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
                _spots.Add(activation.SpotRid, activation);
            }
            ZLinkRuntimeMetrics.RecordSpotCreated("user");

            return new ZLinkSpotCreateResult(
                activation.SpotRid,
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
        RoutingId requestedSpotRid,
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

            if (_spots.TryGetValue(requestedSpotRid, out var existing))
            {
                ThrowIfSpotTypeMismatch(existing.Spot.GetType(), spotType, requestedSpotRid);
                return new ZLinkSpotCreateResult(
                    existing.SpotRid,
                    ZLinkSpotCreateState.Existing,
                    null);
            }

            if (_pending.TryGetValue(requestedSpotRid, out pending!))
            {
                ThrowIfSpotTypeMismatch(pending.SpotType, spotType, requestedSpotRid);
            }
            else
            {
                BeginCreationLocked();
                pending = new PendingSpotCreation(spotType);
                _pending.Add(requestedSpotRid, pending);
                owner = true;
            }
        }

        if (owner)
            _ = CompletePendingCreationAsync(
                spotType,
                requestedSpotRid,
                request,
                pending,
                runtime.ShutdownToken);

        var result = await pending.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
        return !owner && result.State == ZLinkSpotCreateState.Created
            ? result with { State = ZLinkSpotCreateState.Existing }
            : result;
    }

    private async ValueTask CompletePendingCreationAsync(
        Type spotType,
        RoutingId requestedSpotRid,
        ZLinkMessage request,
        PendingSpotCreation pending,
        CancellationToken cancellationToken)
    {
        IZLinkBackendSpot? nativeSpot = null;
        ZLinkSpotActivation? activation = null;
        var factoryOwnsNativeSpot = false;
        try
        {
            nativeSpot = node.GetOrCreateSpot(requestedSpotRid, out var created);
            if (!created)
            {
                var existingNativeSpot = nativeSpot;
                nativeSpot = null;
                await existingNativeSpot.DisposeAsync();
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotCreateFailed,
                    $"SPOT routing id '{requestedSpotRid}' already exists in core but no framework SPOT is registered.");
            }

            factoryOwnsNativeSpot = true;
            var creation = await _activationFactory.CreateAsync(
                spotType,
                nativeSpot,
                request,
                cancellationToken);
            activation = creation.Activation;

            if (!creation.Response.Accepted)
            {
                var rejected = new ZLinkSpotCreateResult(
                    activation.SpotRid,
                    ZLinkSpotCreateState.Rejected,
                    creation.Response.Reply);
                await DisposeFailedCreationAsync(activation).ConfigureAwait(false);
                lock (_gate)
                {
                    _pending.Remove(requestedSpotRid);
                    pending.Complete(rejected);
                }
                return;
            }

            cancellationToken.ThrowIfCancellationRequested();
            await ClaimSpotLocationAsync(activation, spotType, cancellationToken)
                .ConfigureAwait(false);

            var result = new ZLinkSpotCreateResult(
                activation.SpotRid,
                ZLinkSpotCreateState.Created,
                creation.Response.Reply);
            lock (_gate)
            {
                _pending.Remove(requestedSpotRid);
                _spots.Add(activation.SpotRid, activation);
                pending.Complete(result);
            }
            ZLinkRuntimeMetrics.RecordSpotCreated("user");
        }
        catch (Exception error)
        {
            ZLinkFrameworkDebugLog.SpotDiscovery(
                $"SPOT '{requestedSpotRid}' creation failed on node '{node.RoutingId}': {error}");
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
                _pending.Remove(requestedSpotRid);
                pending.Fail(finalFailure);
            }
        }
        finally
        {
            EndCreation();
        }
    }

    public ValueTask<ZLinkSpotInfo?> GetAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            ZLinkSpotInfo? result = _spots.TryGetValue(spotRid, out var activation)
                ? new ZLinkSpotInfo(activation.SpotRid)
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
                .Select(static activation => new ZLinkSpotInfo(activation.SpotRid))
                .OrderBy(static item => item.SpotRid.ToHex(), StringComparer.Ordinal)
                .ToArray();
            return ValueTask.FromResult(result);
        }
    }

    public async ValueTask<bool> CloseAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        ZLinkSpotActivation? activation;
        TaskCompletionSource<bool>? transaction;
        var ownsTransaction = false;
        cancellationToken.ThrowIfCancellationRequested();
        lock (_gate)
        {
            if (_closing.TryGetValue(spotRid, out transaction))
            {
                activation = null;
            }
            else
            {
                if (!_spots.TryGetValue(spotRid, out activation)) return false;

                transaction = new TaskCompletionSource<bool>(
                    TaskCreationOptions.RunContinuationsAsynchronously);
                _closing.Add(spotRid, transaction);
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
                        await ExecuteCloseTransactionAsync(spotRid, activation!, transaction!)
                            .ConfigureAwait(false);
                    }))
            {
                lock (_gate) _closing.Remove(spotRid);
                transaction!.TrySetException(new InvalidOperationException(
                    $"SPOT '{spotRid}' close could not be scheduled in the current runtime generation."));
                return false;
            }

            return true;
        }

        return await ExecuteCloseTransactionAsync(spotRid, activation!, transaction!)
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

        var spotRid = activation.SpotRid;
        var status = await lifecycle.SpotLocations.ClaimAsync(
                spotChannelName,
                spotRid,
                spotType.FullName,
                node.RoutingId,
                ZLinkSpotKind.User,
                deactivate: async ct => _ = await CloseAsync(spotRid, ct).ConfigureAwait(false),
                cancellationToken)
            .ConfigureAwait(false);
        if (status == ZLinkLocationWriteStatus.Stored) return;

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.SpotCreateFailed,
            status == ZLinkLocationWriteStatus.RejectedConflict
                ? $"SPOT '{spotRid}' location in mesh '{spotChannelName}' is owned by another node."
                : $"SPOT '{spotRid}' location claim failed because the location store is unavailable.");
    }

    private async ValueTask ReleaseSpotLocationAsync(RoutingId spotRid)
    {
        if (lifecycle is not null)
            await lifecycle.SpotLocations.ReleaseAsync(spotChannelName, spotRid).ConfigureAwait(false);
    }

    private IReadOnlyCollection<ZLinkSpotActivation> SnapshotActivations()
    {
        lock (_gate)
        {
            return _spots.Values.ToArray();
        }
    }

    private async ValueTask<bool> ExecuteCloseTransactionAsync(
        RoutingId spotRid,
        ZLinkSpotActivation activation,
        TaskCompletionSource<bool> transaction)
    {
        try
        {
            if (!await activation.TryCloseIfNoActorsAsync(CancellationToken.None)
                    .ConfigureAwait(false))
            {
                lock (_gate) _closing.Remove(spotRid);
                transaction.TrySetResult(false);
                return false;
            }

            await ReleaseSpotLocationAsync(spotRid).ConfigureAwait(false);
        }
        catch (Exception releaseFailure)
        {
            lock (_gate) _closing.Remove(spotRid);
            transaction.TrySetException(releaseFailure);
            throw;
        }

        Exception? failure = null;
        try
        {
            await activation.DisposeAsync().ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            failure = failure is null ? exception : new AggregateException(failure, exception);
        }

        lock (_gate)
        {
            _spots.Remove(spotRid);
            _closing.Remove(spotRid);
        }
        if (Volatile.Read(ref _activeDrainMetricPolicy) is { } policy)
            ZLinkRuntimeMetrics.RecordDrainRoom(policy);
        ZLinkRuntimeMetrics.RecordSpotClosed("user");

        if (failure is not null)
        {
            transaction.TrySetException(failure);
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(failure).Throw();
        }

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
        await failures.CaptureAsync(() => ReleaseSpotLocationAsync(activation.SpotRid)).ConfigureAwait(false);
        await failures.CaptureAsync(activation.DisposeAsync).ConfigureAwait(false);
        lock (_gate)
        {
            _spots.Remove(activation.SpotRid);
            _closing.Remove(activation.SpotRid);
        }
        if (Volatile.Read(ref _activeDrainMetricPolicy) is { } policy)
            ZLinkRuntimeMetrics.RecordDrainRoom(policy);
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
        if (activation is not null) _spots.Remove(activation.SpotRid);
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
        RoutingId spotRid)
    {
        if (existingSpotType == requestedSpotType) return;

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.SpotTypeMismatch,
            $"SPOT routing id '{spotRid}' already belongs to '{existingSpotType}'.");
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
