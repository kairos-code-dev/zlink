using Microsoft.Extensions.DependencyInjection;

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
    private readonly ZLinkSpotRetireScheduler? _retireScheduler =
        CreateRetireScheduler(
            services,
            runtime,
            frameworkRegistration);

    private readonly object _gate = new();
    private readonly Dictionary<string, TaskCompletionSource<bool>> _closing = [];
    private readonly Dictionary<string, string> _instanceSpotTypes =
        new(StringComparer.Ordinal);
    private readonly Dictionary<string, string> _pendingInstanceSpotTypes =
        new(StringComparer.Ordinal);
    private readonly Dictionary<string, PendingSpotCreation> _pending = [];
    private readonly Dictionary<string, ZLinkSpotActivation> _spots = [];
    private TaskCompletionSource? _creationsDrained;
    private int _activeCreations;
    private bool _closed;

    public IReadOnlyCollection<ZLinkSpotActivation> Spots => SnapshotActivations();

    internal ZLinkInstanceSpotCatalogSnapshot InstanceSpotSnapshot(
        string stableType)
    {
        lock (_gate)
        {
            var active = _instanceSpotTypes.Count(entry =>
                StringComparer.Ordinal.Equals(entry.Value, stableType)
                && _spots.ContainsKey(entry.Key));
            var activating = _pendingInstanceSpotTypes.Count(entry =>
                StringComparer.Ordinal.Equals(entry.Value, stableType)
                && _pending.ContainsKey(entry.Key));
            var closing = _closing.Keys.Count(spotId =>
                _instanceSpotTypes.TryGetValue(
                    spotId,
                    out var currentType)
                && StringComparer.Ordinal.Equals(
                    currentType,
                    stableType));
            return new ZLinkInstanceSpotCatalogSnapshot(
                checked((ulong)active),
                checked((ulong)activating),
                checked((ulong)closing));
        }
    }

    internal async ValueTask<bool> TryDrainAsync(CancellationToken cancellationToken)
    {
        var activations = SnapshotActivations();
        foreach (var activation in activations)
            await CloseAsync(activation.SpotId, cancellationToken).ConfigureAwait(false);

        lock (_gate) return _spots.Count == 0;
    }

    internal ValueTask<ZLinkFrameworkTerminationReason?> PreflightRetireAsync(
        ZLinkRetirePreflightPlan plan,
        CancellationToken cancellationToken)
    {
        (ZLinkSpotActivation Activation, bool Instance)[] units;
        lock (_gate)
        {
            if (_spots.Count == 0)
                return ValueTask.FromResult<ZLinkFrameworkTerminationReason?>(null);
            if (_retireScheduler is null)
                return ValueTask.FromResult<ZLinkFrameworkTerminationReason?>(
                    ZLinkFrameworkTerminationReason.RelocationDisabled);
            units = _spots.Values
                .Select(activation => (
                    activation,
                    _instanceSpotTypes.ContainsKey(activation.SpotId)))
                .ToArray();
        }
        return _retireScheduler.PreflightAsync(units, plan, cancellationToken);
    }

    internal async ValueTask<ZLinkSpotDrainResult> TryRelocateForRetireAsync(
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        (ZLinkSpotActivation Activation, bool Instance)[] units;
        lock (_gate)
        {
            if (_spots.Count == 0)
                return new ZLinkSpotDrainResult(true, 0);
            if (_retireScheduler is null)
                return new ZLinkSpotDrainResult(false, 0);
            units = _spots.Values
                .Select(activation => (
                    activation,
                    _instanceSpotTypes.ContainsKey(activation.SpotId)))
                .ToArray();
        }

        var deadline = DateTimeOffset.UtcNow
                       + units.Select(static unit =>
                               unit.Activation.DefaultRequestTimeout)
                           .DefaultIfEmpty(TimeSpan.FromSeconds(30))
                           .Max();
        var moves = units.Select(unit => RelocateAsync(unit).AsTask()).ToArray();
        var results = await Task.WhenAll(moves).ConfigureAwait(false);
        return new ZLinkSpotDrainResult(
            results.All(static result => result),
            checked((ulong)results.Count(static result => result)));

        async ValueTask<bool> RelocateAsync(
            (ZLinkSpotActivation Activation, bool Instance) unit)
        {
            try
            {
                return await _retireScheduler.TryRelocateAsync(
                        unit.Activation,
                        unit.Instance,
                        deadline,
                        CompleteRelocatedSourceAsync,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (OperationCanceledException)
                when (cancellationToken.IsCancellationRequested)
            {
                throw;
            }
            catch (ZLinkAuthorityGenerationExhaustedException)
            {
                throw;
            }
            catch
            {
                return false;
            }
        }
    }

    private async ValueTask CompleteRelocatedSourceAsync(
        ZLinkSpotActivation activation,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        lifecycle?.SpotLocations.ForgetRelocated(
            activation.SpotId,
            activation.ObjectGeneration);
        lock (_gate)
        {
            _spots.Remove(activation.SpotId);
            _instanceSpotTypes.Remove(activation.SpotId);
            _closing.Remove(activation.SpotId);
        }
        var forwardingRemaining = activation.CommittedForwardingRemaining;
        if (forwardingRemaining <= TimeSpan.Zero)
        {
            await activation.DisposeAsync().ConfigureAwait(false);
        }
        else if (!runtime.TryRunDetached(
                     "spot-relocation-forwarding-window",
                     async cancellationToken =>
                     {
                         try
                         {
                             await Task.Delay(
                                     forwardingRemaining,
                                     cancellationToken)
                                 .ConfigureAwait(false);
                         }
                         catch (OperationCanceledException)
                         {
                         }
                         await activation.DisposeAsync()
                             .ConfigureAwait(false);
                     }))
        {
            await activation.DisposeAsync().ConfigureAwait(false);
        }
        ZLinkRuntimeMetrics.RecordSpotClosed(
            activation.Spot is IZLinkInstanceSpot ? "instance" : "user");
    }

    private static ZLinkSpotRetireScheduler? CreateRetireScheduler(
        IServiceProvider services,
        ZLinkFrameworkRuntime runtime,
        ZLinkFrameworkRegistration registration)
    {
        var location = registration.Locations.ResolveStore();
        var relocation = registration.Locations.RelocationStoreInstance;
        var target = services.GetService<IZLinkSpotRetireTarget>();
        return location is null || relocation is null || target is null
            ? null
            : new ZLinkSpotRetireScheduler(
                location,
                relocation,
                target,
                runtime.RelocationPermits);
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
                                transaction,
                                ZLinkSpotCloseReason.HostShutdown,
                                DateTimeOffset.UtcNow + activation.DefaultRequestTimeout)
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
                _instanceSpotTypes.Remove(spotId);
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
            _ = CompleteReservedCreationAsync(
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

    private async ValueTask CompleteReservedCreationAsync(
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
        ulong objectGeneration,
        ulong authorityOwnerGeneration,
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
            nativeSpot = node.GetOrCreateReservedSpot(
                requestedSpotId,
                objectGeneration,
                authorityOwnerGeneration,
                out var created);
            if (!created)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotMoving,
                    $"SPOT '{requestedSpotId}' is already materialized.");
            var creation = await _activationFactory.CreateAsync(
                    spotType, nativeSpot, requestedSpotId, request, cancellationToken)
                .ConfigureAwait(false);
            activation = creation.Activation;
            lock (_gate)
            {
                _pending.Remove(requestedSpotId);
                _pendingInstanceSpotTypes.Remove(requestedSpotId);
            }
            EndCreation();
            return new PreparedReservedSpot(
                activation,
                false,
                creation.Response);
        }
        catch
        {
            lock (_gate)
            {
                _pending.Remove(requestedSpotId);
                _pendingInstanceSpotTypes.Remove(requestedSpotId);
            }
            if (activation is not null)
                await activation.DisposeAsync().ConfigureAwait(false);
            else if (nativeSpot is not null)
                await nativeSpot.DisposeAsync().ConfigureAwait(false);
            EndCreation();
            throw;
        }
    }

    internal async ValueTask<PreparedReservedSpot> PrepareInstanceReservedAsync(
        string stableType,
        string requestedSpotId,
        ulong objectGeneration,
        ulong authorityOwnerGeneration,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!registration.InstanceSpotFactories.TryGetValue(
                stableType,
                out var factory))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SpotTypeMismatch,
                $"Instance Spot type '{stableType}' is not registered.");

        var pending = new PendingSpotCreation(factory.SpotType);
        lock (_gate)
        {
            EnsureCreationAdmissionOpenLocked();
            if (_spots.TryGetValue(requestedSpotId, out var existing))
            {
                ThrowIfSpotTypeMismatch(
                    existing.Spot.GetType(),
                    factory.SpotType,
                    requestedSpotId);
                return new PreparedReservedSpot(
                    existing,
                    true,
                    null,
                    stableType);
            }
            if (_pending.ContainsKey(requestedSpotId))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotMoving,
                    $"Instance Spot '{requestedSpotId}' is already being materialized.",
                    true);

            BeginCreationLocked();
            _pending.Add(requestedSpotId, pending);
            _pendingInstanceSpotTypes.Add(requestedSpotId, stableType);
        }

        IZLinkBackendSpot? nativeSpot = null;
        ZLinkSpotActivation? activation = null;
        try
        {
            nativeSpot = node.GetOrCreateReservedSpot(
                requestedSpotId,
                objectGeneration,
                authorityOwnerGeneration,
                out var created);
            if (!created)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotMoving,
                    $"Instance Spot '{requestedSpotId}' is already materialized.");
            activation = await _activationFactory.CreateInstanceAsync(
                    factory.SpotType,
                    nativeSpot,
                    requestedSpotId,
                    cancellationToken)
                .ConfigureAwait(false);
            lock (_gate)
            {
                _pending.Remove(requestedSpotId);
                _pendingInstanceSpotTypes.Remove(requestedSpotId);
            }
            EndCreation();
            return new PreparedReservedSpot(
                activation,
                false,
                null,
                stableType);
        }
        catch
        {
            lock (_gate)
            {
                _pending.Remove(requestedSpotId);
                _pendingInstanceSpotTypes.Remove(requestedSpotId);
            }
            if (activation is not null)
                await activation.DisposeAsync().ConfigureAwait(false);
            else if (nativeSpot is not null)
                await nativeSpot.DisposeAsync().ConfigureAwait(false);
            EndCreation();
            throw;
        }
    }

    internal async ValueTask PublishReservedAsync(
        PreparedReservedSpot prepared,
        string stableType,
        ulong objectGeneration,
        ulong authorityOwnerGeneration,
        CancellationToken cancellationToken)
    {
        if (prepared.Existing) return;
        await ClaimSpotLocationAsync(
                prepared.Activation,
                stableType,
                objectGeneration,
                authorityOwnerGeneration,
                cancellationToken)
            .ConfigureAwait(false);
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

    internal ValueTask ValidateRelocatedReservedAsync(
        PreparedReservedSpot prepared,
        string stableType,
        ulong objectGeneration,
        ulong authorityOwnerGeneration,
        CancellationToken cancellationToken) =>
        prepared.Existing || lifecycle is null
            ? ValueTask.CompletedTask
            : ValidateRelocatedReservedCoreAsync(
                prepared,
                stableType,
                objectGeneration,
                authorityOwnerGeneration,
                cancellationToken);

    private async ValueTask ValidateRelocatedReservedCoreAsync(
        PreparedReservedSpot prepared,
        string stableType,
        ulong objectGeneration,
        ulong authorityOwnerGeneration,
        CancellationToken cancellationToken)
    {
        var activation = prepared.Activation;
        var kind = activation.Spot is IZLinkInstanceSpot
            ? ZLinkSpotKind.Instance
            : ZLinkSpotKind.User;
        var status = await lifecycle!.SpotLocations.TrackRelocatedAsync(
                spotChannelName,
                activation.SpotId,
                objectGeneration,
                authorityOwnerGeneration,
                stableType,
                node.RoutingId,
                node.MeshStatus().LifecycleGeneration,
                kind,
                deactivate: async ct =>
                    _ = await CloseAsync(activation.SpotId, ct)
                        .ConfigureAwait(false),
                cancellationToken)
            .ConfigureAwait(false);
        if (status != ZLinkLocationWriteStatus.Stored)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SpotMoving,
                $"SPOT '{activation.SpotId}' relocation authority is not published for this target.",
                true);
    }

    internal void PublishRelocatedReserved(PreparedReservedSpot prepared)
    {
        if (prepared.Existing) return;
        lock (_gate)
        {
            if (_spots.ContainsKey(prepared.Activation.SpotId))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotMoving,
                    $"SPOT '{prepared.Activation.SpotId}' became visible before relocation publication.");
            _spots.Add(prepared.Activation.SpotId, prepared.Activation);
            if (prepared.Activation.Spot is IZLinkInstanceSpot)
                _instanceSpotTypes.Add(
                    prepared.Activation.SpotId,
                    prepared.InstanceStableType
                    ?? throw new InvalidOperationException(
                        "Instance Spot stable type is missing."));
        }
        ZLinkRuntimeMetrics.RecordSpotCreated(
            prepared.Activation.Spot is IZLinkInstanceSpot
                ? "instance"
                : "user");
    }

    internal void PublishInstanceReserved(PreparedReservedSpot prepared)
    {
        if (prepared.Existing) return;
        lock (_gate)
        {
            if (_spots.ContainsKey(prepared.Activation.SpotId))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotMoving,
                    $"Instance Spot '{prepared.Activation.SpotId}' became visible before publication.");
            _spots.Add(prepared.Activation.SpotId, prepared.Activation);
            _instanceSpotTypes.Add(
                prepared.Activation.SpotId,
                prepared.InstanceStableType
                ?? throw new InvalidOperationException(
                    "Instance Spot stable type is missing."));
        }
        ZLinkRuntimeMetrics.RecordSpotCreated("instance");
    }

    internal bool TryGetInstanceActivation(
        string spotId,
        string stableType,
        ulong objectGeneration,
        out ZLinkSpotActivation activation)
    {
        lock (_gate)
        {
            if (_spots.TryGetValue(spotId, out var existing)
                && existing.Spot is IZLinkInstanceSpot
                && existing.NativeSpot.LifecycleGeneration == objectGeneration
                && registration.InstanceSpotFactories.TryGetValue(
                    stableType,
                    out var factory)
                && factory.SpotType.IsInstanceOfType(existing.Spot))
            {
                activation = existing;
                return true;
            }
        }

        activation = null!;
        return false;
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
        return await CloseAsync(spotId, null, cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask<bool> CloseAsync(
        string spotId,
        DateTimeOffset? deadline,
        CancellationToken cancellationToken)
    {
        return await CloseCoreAsync(
                spotId,
                deadline,
                releaseLocation: true,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal ValueTask<bool> CloseReservedAsync(
        string spotId,
        DateTimeOffset? deadline,
        CancellationToken cancellationToken) =>
        CloseCoreAsync(
            spotId,
            deadline,
            releaseLocation: false,
            cancellationToken);

    private async ValueTask<bool> CloseCoreAsync(
        string spotId,
        DateTimeOffset? deadline,
        bool releaseLocation,
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
                        await ExecuteCloseTransactionAsync(
                                spotId,
                                activation!,
                                transaction!,
                                ZLinkSpotCloseReason.ExplicitClose,
                                deadline,
                                releaseLocation)
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

        return await ExecuteCloseTransactionAsync(
                spotId,
                activation!,
                transaction!,
                ZLinkSpotCloseReason.ExplicitClose,
                deadline,
                releaseLocation)
            .ConfigureAwait(false);
    }

    /// <summary>A created User Spot claims its authority before it becomes
    /// addressable. A claim that cannot be stored fails creation because an
    /// unadvertised or doubly claimed Spot would break single activation.</summary>
    private async ValueTask ClaimSpotLocationAsync(
        ZLinkSpotActivation activation,
        Type spotType,
        CancellationToken cancellationToken)
    {
        await ClaimSpotLocationAsync(
                activation,
                spotType.FullName,
                activation.NativeSpot.LifecycleGeneration,
                authorityOwnerGeneration: 0,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask ClaimSpotLocationAsync(
        ZLinkSpotActivation activation,
        string? spotType,
        ulong objectGeneration,
        ulong authorityOwnerGeneration,
        CancellationToken cancellationToken)
    {
        if (lifecycle is null) return;

        var spotId = activation.SpotId;
        var status = await lifecycle.SpotLocations.ClaimAsync(
                spotChannelName,
                spotId,
                objectGeneration,
                spotType,
                node.RoutingId,
                node.MeshStatus().LifecycleGeneration,
                ZLinkSpotKind.User,
                authorityOwnerGeneration,
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
        TaskCompletionSource<bool> transaction,
        ZLinkSpotCloseReason reason,
        DateTimeOffset? deadline,
        bool releaseLocation = true)
    {
        try
        {
            if (!await activation.TryCloseIfNoActorsAsync(
                    reason,
                    deadline ?? DateTimeOffset.UtcNow + activation.DefaultRequestTimeout,
                    CancellationToken.None)
                    .ConfigureAwait(false))
            {
                lock (_gate) _closing.Remove(spotId);
                transaction.TrySetResult(false);
                return false;
            }

            await activation.DisposeAsync().ConfigureAwait(false);
            if (releaseLocation)
                await ReleaseSpotLocationAsync(spotId).ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            lock (_gate)
            {
                _spots.Remove(spotId);
                _instanceSpotTypes.Remove(spotId);
                _closing.Remove(spotId);
            }
            transaction.TrySetException(exception);
            throw;
        }

        lock (_gate)
        {
            _spots.Remove(spotId);
            _instanceSpotTypes.Remove(spotId);
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
        await failures.CaptureAsync(
                () => activation.CloseAsync(
                    ZLinkSpotCloseReason.HostShutdown,
                    DateTimeOffset.UtcNow + activation.DefaultRequestTimeout,
                    CancellationToken.None))
            .ConfigureAwait(false);
        await failures.CaptureAsync(() => ReleaseSpotLocationAsync(activation.SpotId)).ConfigureAwait(false);
        await failures.CaptureAsync(activation.DisposeAsync).ConfigureAwait(false);
        lock (_gate)
        {
            _spots.Remove(activation.SpotId);
            _instanceSpotTypes.Remove(activation.SpotId);
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
        if (activation is not null)
        {
            _spots.Remove(activation.SpotId);
            _instanceSpotTypes.Remove(activation.SpotId);
        }
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

internal readonly record struct ZLinkSpotDrainResult(
    bool Completed,
    ulong CommittedUnitCount)
{
    internal bool HasCommitted => CommittedUnitCount != 0;
}

internal sealed record PreparedReservedSpot(
    ZLinkSpotActivation Activation,
    bool Existing,
    ZLinkSpotCreateResponse? Response,
    string? InstanceStableType = null)
{
    internal Type SpotType => Activation.Spot.GetType();
}

internal enum ReservedSpotCloseReadiness
{
    Ready,
    HasActors,
    LocalMissing,
    Closing
}
