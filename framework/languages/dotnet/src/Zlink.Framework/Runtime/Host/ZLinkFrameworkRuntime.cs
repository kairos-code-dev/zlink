using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Runtime.Dispatch;

namespace Zlink.Framework.Runtime.Host;

internal interface IZLinkRuntimeTerminalFailureSink
{
    void SealError(Exception error);
}

internal readonly record struct CreateActorResult(
    IZLinkActor Actor,
    bool Created,
    ZLinkMessage CreateRequest,
    ZLinkActorCreateResponse? Response = null);

internal readonly record struct ZLinkDrainRemainderCounts(
    int Actors,
    int Spots,
    int Requests,
    int Sessions);

internal sealed partial class ZLinkFrameworkRuntime : IZLinkSpotManager
{
    private static readonly AsyncLocal<ZLinkRuntimeOperationOwnership?> AmbientOperation = new();
    private readonly ZLinkActorDrainCoordinator _actorDrainCoordinator;
    private readonly ZLinkStandaloneActorRelocationRuntime
        _standaloneActorRelocationRuntime;
    private readonly ZLinkActorBoundSessionCoordinator _actorBoundSessionCoordinator;
    private readonly ZLinkFrameworkActorFacade _actors;
    private readonly ZLinkActorSessionManager _actorSessionManager;
    private readonly ZLinkActorHandoffAdmissions _actorHandoffAdmissions;
    private readonly IZLinkBackendAdapterFactory _backendAdapterFactory;
    private readonly ZLinkLocationAutoConnectHost? _autoConnect;
    private readonly ZLinkChannelRuntimeManager _channels;
    private readonly ZLinkDrainAdmissionGate _drainAdmission;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private ZLinkRuntimeExecutionScope? _executionScope;
    private readonly object _operationGate = new();
    private readonly ZLinkLocationLifecycle? _locationLifecycle;
    private readonly ZLinkLocationRuntime? _locationRuntime;
    private readonly ZLinkRelocationPermitPool _relocationPermits;
    private readonly ZLinkRelocationInterruptionObserver
        _relocationInterruption;
    private readonly IZLinkAutoConnectTopologyQuery? _topologyQuery;
    private readonly ZLinkSpotRouteRouterDispatcher _spotRouteRouter;
    private readonly ZLinkSpotRuntimeManager _spots;
    private readonly ZLinkFrameworkComponentStateFactory _stateFactory;
    private readonly ZLinkStreamRuntimeManager _streams;
    private readonly object _workerPoolGate = new();
    private ZLinkMessageFlowTracer? _flow;
    private ILogger? _actorHandoffLogger;
    private ILogger? _timerLogger;
    private ZLinkRuntimeErrorSink? _generationErrorSink = new();
    private int _lifecyclePhase;
    private ZLinkFrameworkComponentState? _state;
    private ZLinkWorkerPool? _workerPool;
    private TaskCompletionSource? _operationsDrained;
    private int _activeOperations;
    private int _activeRequests;
    private bool _acceptingOperations;
    private long _nextInstanceActivationSelection;
    private ZLinkRelocationTargetSelection _relocationTargetSelection;

    public ZLinkFrameworkRuntime(
        IServiceProvider services,
        IZLinkBackendAdapterFactory backendAdapterFactory,
        ZLinkFrameworkRegistration registration,
        ZLinkHandlerRegistry handlerRegistry,
        ZLinkHandlerDispatcher dispatcher)
    {
        Services = services;
        _actorHandoffAdmissions = new ZLinkActorHandoffAdmissions(
            diagnostic: LogActorHandoff,
            abortCapacityReservation:
                AbortActorHandoffCapacityReservationAsync);
        _backendAdapterFactory = backendAdapterFactory;
        _autoConnect = services.GetService<ZLinkLocationAutoConnectHost>();
        Registration = registration;
        _drainAdmission = services.GetService<ZLinkDrainAdmissionGate>() ?? new ZLinkDrainAdmissionGate();
        _locationLifecycle = services.GetService<ZLinkLocationLifecycle>();
        _locationRuntime = services.GetService<ZLinkLocationRuntime>();
        _topologyQuery = services.GetService<IZLinkAutoConnectTopologyQuery>();
        _relocationPermits = new ZLinkRelocationPermitPool(
            registration.Locations.Options);
        _relocationInterruption =
            new ZLinkRelocationInterruptionObserver(
                services.GetService<ILoggerFactory>());
        var components = ZLinkFrameworkRuntimeComponentFactory.Create(
            this,
            services,
            backendAdapterFactory,
            registration,
            _locationLifecycle,
            handlerRegistry,
            dispatcher,
            GetOrStartState,
            GetActorSpotNode);
        _channels = components.Channels;
        _streams = components.Streams;
        _spots = components.Spots;
        _stateFactory = components.StateFactory;
        _actorSessionManager = components.ActorSessionManager;
        _actors = components.Actors;
        _actorBoundSessionCoordinator = new ZLinkActorBoundSessionCoordinator(
            _actorSessionManager.GetOrCreateState,
            () => GetActorSpotNode() ?? GetRouterSpotNodeOrNull(),
            meshName => GetMeshNodeRuntime(meshName).Node,
            registration,
            () => ShutdownToken)
        {
            RemotePushRelay = RelayRemoteSessionPush,
            RemoteFrameRelay = RelayRemoteActorFrame
        };
        _standaloneActorRelocationRuntime =
            new ZLinkStandaloneActorRelocationRuntime(
                this,
                _actorSessionManager,
                registration);
        _actorDrainCoordinator = new ZLinkActorDrainCoordinator(
            _standaloneActorRelocationRuntime,
            _actorSessionManager,
            services,
            registration);
        _actorMessageFollower = new ZLinkActorMessageFollower(this);
        _spotRouteRouter = new ZLinkSpotRouteRouterDispatcher(GetOrStartState);
    }

    public IZLinkBackendContext? Context
        => Volatile.Read(ref _lifecyclePhase) == (int)ZLinkRuntimeLifecyclePhase.Running
            ? _state?.Context
            : null;

    public ZLinkFrameworkRegistration Registration { get; }

    internal ZLinkStandaloneActorRelocationRuntime
        StandaloneActorRelocationRuntime => _standaloneActorRelocationRuntime;

    // Shared success-path tracer for outbound client calls (channel/route/spot/actor
    // send/request/publish), built once. Inbound surfaces use the reporter's Flow.
    internal ZLinkMessageFlowTracer Flow => _flow ??= new ZLinkMessageFlowTracer(
        Registration.DispatchOptions,
        ZLinkMessageFlowTracer.CreateLogger(Services.GetService<ILoggerFactory>()),
        this);

    internal void LogActorHandoff(string marker)
    {
        _actorHandoffLogger ??= Services.GetService<ILoggerFactory>()?
            .CreateLogger("Zlink.Framework.ActorHandoff");
        _actorHandoffLogger?.LogInformation("{ActorHandoffMarker}", marker);
    }

    internal ZLinkRuntimeErrorSink ErrorSink => Volatile.Read(ref _generationErrorSink)
                                                 ?? throw new InvalidOperationException(
                                                     "The framework runtime error sink is not active.");

    internal ZLinkRuntimeErrorSink PrepareErrorSink()
    {
        var current = Volatile.Read(ref _generationErrorSink);
        if (current is not null) return current;
        var created = new ZLinkRuntimeErrorSink();
        return Interlocked.CompareExchange(ref _generationErrorSink, created, null) ?? created;
    }

    internal void DetachErrorSink(ZLinkRuntimeErrorSink errorSink) =>
        Interlocked.CompareExchange(ref _generationErrorSink, null, errorSink);

    internal void TryReportUnhandledCallbackException(Exception exception) =>
        Volatile.Read(ref _generationErrorSink)?.ReportUnhandledCallbackException(exception);

    internal void ReportTimerFailure(
        string sourceName,
        string spotId,
        bool isEntrySpot,
        string timerName,
        Type handlerType,
        ZLinkTimerTick tick,
        Exception exception,
        bool stopped)
    {
        TryReportUnhandledCallbackException(exception);
        _timerLogger ??= Services.GetService<ILoggerFactory>()?
            .CreateLogger("Zlink.Framework.SpotTimer");
        _timerLogger?.LogError(
            exception,
            "Spot timer handler failed. source={SourceName} spot={SpotId} entry={IsEntrySpot} timer={TimerName} handler={HandlerType} delivery={DeliveryIndex} scheduled={ScheduledIndex} stopped={Stopped}",
            sourceName,
            spotId,
            isEntrySpot,
            timerName,
            handlerType.FullName ?? handlerType.Name,
            tick.DeliveryIndex,
            tick.ScheduledIndex,
            stopped);
    }

    internal IServiceProvider Services { get; }

    internal ZLinkDrainAdmissionGate DrainAdmission => _drainAdmission;

    internal ZLinkRelocationPermitPool RelocationPermits =>
        _relocationPermits;

    internal ZLinkRelocationInterruptionObserver
        RelocationInterruption => _relocationInterruption;

    internal async ValueTask<bool> DrainStreamSessionsAsync(CancellationToken cancellationToken)
    {
        var state = _state;
        if (state is null) return true;
        var drained = true;
        foreach (var streamNode in state.StreamNodes.Values)
            drained &= await streamNode.DrainSessionsAsync(cancellationToken).ConfigureAwait(false);
        return drained;
    }

    internal async ValueTask<ZLinkSpotDrainResult> TryDrainSpotsAsync(
        bool relocate,
        CancellationToken cancellationToken)
        => await TryDrainSpotsAsync(
                relocate,
                ZLinkSpotRelocationPhase.Aggregates,
                cancellationToken)
            .ConfigureAwait(false);

    private async ValueTask<ZLinkSpotDrainResult> TryDrainSpotsAsync(
        bool relocate,
        ZLinkSpotRelocationPhase phase,
        CancellationToken cancellationToken)
    {
        var state = _state;
        if (state is null) return new ZLinkSpotDrainResult(true, 0);
        var drained = true;
        ulong committedUnitCount = 0;
        foreach (var spotNode in state.SpotNodes.Values)
        {
            var result = await spotNode.TryDrainSpotsAsync(
                    relocate,
                    _relocationTargetSelection,
                    phase,
                    cancellationToken)
                .ConfigureAwait(false);
            drained &= result.Completed;
            committedUnitCount = checked(
                committedUnitCount + result.CommittedUnitCount);
        }
        return new ZLinkSpotDrainResult(drained, committedUnitCount);
    }

    internal async ValueTask<ZLinkRelocationWorkloadDrainResult>
        DrainRelocationWorkloadsAsync(
            ZLinkRelocationWorkloadDrainControl control)
        => await new ZLinkRelocationWorkloadCoordinator(
                (phase, token) => TryDrainSpotsAsync(
                    relocate: true,
                    phase,
                    token),
                DrainActorsAsync)
            .DrainAsync(control)
            .ConfigureAwait(false);

    internal async ValueTask<bool> QuiesceServingChannelsForDrainAsync(
        ZLinkLocationAutoConnectHost? autoConnect,
        CancellationToken cancellationToken)
    {
        var state = _state;
        if (state is null) return true;

        // Once the marker propagation window closes, advertise zero weight before request
        // admissions are sealed. Requests that arrived during the contract's propagation window
        // were still processed; later traffic is removed from peer load balancing before owner
        // cleanup can terminate the pipes.
        var published = true;
        foreach (var (name, spotNode) in state.SpotNodes)
        {
            if (!Registration.SpotNodes.TryGetValue(name, out var registration))
                continue;

            foreach (var membership in registration.ChannelMemberships)
            {
                // Client membership does not own a serving weight. Only a
                // RouteMesh Channel Server is removed from new selection
                // before drain seals application admission.
                if (membership.IsServer)
                    spotNode.Node.SetChannelWeight(
                        membership.ChannelName,
                        0);
            }

            var meshName = registration.SpotMeshChannelName ?? registration.SpotNodeName;
            published &= await PublishWeightAsync(
                autoConnect,
                ZLinkLocationAutoConnectType.SpotMesh,
                meshName,
                ZLinkLocationRole.Spot,
                cancellationToken).ConfigureAwait(false);
        }

        return published;
    }

    private static ValueTask<bool> PublishWeightAsync(
        ZLinkLocationAutoConnectHost? autoConnect,
        ZLinkLocationAutoConnectType type,
        string meshName,
        ZLinkLocationRole role,
        CancellationToken cancellationToken) =>
        autoConnect is null
            ? ValueTask.FromResult(true)
            : autoConnect.SetLocalWeightAsync(type, meshName, role, 0, cancellationToken);

    internal void SealApplicationAdmissionsForDrain()
    {
        lock (_operationGate)
        {
            _drainAdmission.Seal();
            _acceptingOperations = false;
        }
    }

    internal void ReopenRetireAdmissionsAfterRollback()
    {
        lock (_operationGate)
        {
            _drainAdmission.Reset();
            _acceptingOperations = true;
        }
    }

    internal Task WaitForAcceptedOperationsForDrainAsync()
    {
        lock (_operationGate)
            return _activeOperations == 0
                ? Task.CompletedTask
                : (_operationsDrained ??= new TaskCompletionSource(
                    TaskCreationOptions.RunContinuationsAsynchronously)).Task;
    }

    internal async Task WaitForAcceptedActorHandoffsAsync(CancellationToken cancellationToken)
    {
        await _drainAdmission.WaitForAcceptedActorAdmissionsAsync(cancellationToken)
            .ConfigureAwait(false);
        await _actorHandoffAdmissions.WaitUntilDrainSafeAsync(cancellationToken)
            .ConfigureAwait(false);
    }

    internal ZLinkDrainRemainderCounts GetDrainRemainderCounts()
    {
        var actors = _actorSessionManager.SnapshotStates()
            .Count(static actor => actor.Actor is not null);
        var state = _state;
        var spots = state?.SpotNodes.Values.Sum(static node => node.Spots.Count) ?? 0;
        var sessions = state?.StreamNodes.Values.Sum(static node => node.SessionCount) ?? 0;
        int requests;
        lock (_operationGate) requests = _activeRequests;
        return new ZLinkDrainRemainderCounts(actors, spots, requests, sessions);
    }

    internal object ExecutionOwner
    {
        get
        {
            var current = Volatile.Read(ref _executionScope);
            if (current is not null) return current;
            var created = new ZLinkRuntimeExecutionScope();
            return Interlocked.CompareExchange(ref _executionScope, created, null) ?? created;
        }
    }

    internal ZLinkRuntimeOperationLease EnterOperation(bool countAsRequest = false)
    {
        if (AmbientOperation.Value is { IsActive: true } current
            && ReferenceEquals(current.Runtime, this))
        {
            EnsureAmbientOperationCurrent(current);
            if (!countAsRequest) return new ZLinkRuntimeOperationLease();
            lock (_operationGate) _activeRequests++;
            return new ZLinkRuntimeOperationLease(this, countsRequest: true);
        }

        lock (_operationGate)
            return EnterOperationUnderLock(countAsRequest);
    }

    internal ZLinkRuntimeOperationLease RetainOperationForBackgroundWork()
    {
        if (AmbientOperation.Value is not { IsActive: true } current
            || !ReferenceEquals(current.Runtime, this))
            throw new InvalidOperationException(
                "Background work can retain only the current runtime operation.");
        EnsureAmbientOperationCurrent(current);

        lock (_operationGate)
        {
            _activeOperations++;
            return new ZLinkRuntimeOperationLease(
                this,
                countsOperation: true,
                countsRequest: false);
        }
    }

    /// <summary>
    /// <paramref name="ownsObjectWork"/>: false for dispatch that only invokes
    /// a registered handler - channel, node route and fanout records. Spec 21
    /// §4 blocks an expired owner from descriptor publishing, Actor, Spot and
    /// Instance message and timer starts, factory and restore commits, and
    /// relocation state, because "만료된 owner 자격으로 새 Store 변경을 만들지
    /// 않는다". Handler invocation makes no Store change, so a Location Store
    /// outage must not turn it away. The drain seal still applies to every
    /// caller.
    /// </summary>
    internal bool TryEnterInboundOperation(
        bool countAsRequest,
        out ZLinkRuntimeOperationLease lease,
        bool ownsObjectWork = true)
    {
        if (AmbientOperation.Value is { IsActive: true } current
            && ReferenceEquals(current.Runtime, this))
        {
            if (!IsAmbientOperationCurrent(current))
            {
                lease = default!;
                return false;
            }
            if (!countAsRequest)
            {
                lease = new ZLinkRuntimeOperationLease();
                return true;
            }
            lock (_operationGate) _activeRequests++;
            lease = new ZLinkRuntimeOperationLease(this, countsRequest: true);
            return true;
        }

        lock (_operationGate)
        {
            if (_drainAdmission.IsSealed
                || (ownsObjectWork
                    && _locationRuntime is not null
                    && !_locationRuntime.IsOwnerAdmissionOpen))
            {
                lease = new ZLinkRuntimeOperationLease();
                return false;
            }
            // Before native startup no transport can deliver a record. A
            // neutral lease keeps dispatcher construction independent from
            // runtime startup while the drain seal remains authoritative.
            if (Volatile.Read(ref _lifecyclePhase) != (int)ZLinkRuntimeLifecyclePhase.Running)
            {
                lease = new ZLinkRuntimeOperationLease();
                return true;
            }
            lease = EnterOperationUnderLock(countAsRequest);
            return true;
        }
    }

    internal async ValueTask ExecuteOperationAsync(Func<ValueTask> operation)
    {
        using var lease = EnterOperation();
        await operation().ConfigureAwait(false);
    }

    internal async ValueTask<T> ExecuteOperationAsync<T>(Func<ValueTask<T>> operation)
    {
        using var lease = EnterOperation();
        return await operation().ConfigureAwait(false);
    }

    internal T ExecuteOperation<T>(Func<T> operation)
    {
        using var lease = EnterOperation();
        return operation();
    }

    internal ZLinkWorkerPool WorkerPool
    {
        get
        {
            if (!IsStarted)
                throw new InvalidOperationException("ZLink framework runtime is not running.");
            lock (_workerPoolGate)
            {
                if (!IsStarted || _state is null)
                    throw new InvalidOperationException("ZLink framework runtime is not running.");
                return _workerPool ??= Registration.WorkerOptions.CreatePool();
            }
        }
    }

    internal IZLinkRouteClient RouteClient => Services.GetRequiredService<IZLinkRouteClient>();

    internal ZLinkSpotNodeRuntime GetSpotNodeRuntime(RoutingId nodeRid)
    {
        var state = _state
                    ?? throw new InvalidOperationException(
                        "The framework runtime is not started.");
        return state.SpotNodes.Values.SingleOrDefault(
                   node => node.Node.RoutingId == nodeRid)
               ?? throw new ZLinkFrameworkException(
                   ZLinkFrameworkErrorKind.NotFound,
                   $"MeshNode '{nodeRid}' is not hosted by this runtime.");
    }

    internal ZLinkSpotNodeRuntime? TryGetSpotNodeRuntime(RoutingId nodeRid) =>
        _state?.SpotNodes.Values.SingleOrDefault(
            node => node.Node.RoutingId == nodeRid);

    public bool IsStarted
        => Volatile.Read(ref _lifecyclePhase) == (int)ZLinkRuntimeLifecyclePhase.Running;

    internal CancellationToken ShutdownToken
        => _state?.StopTokenSource.Token ?? new CancellationToken(canceled: true);

    internal ZLinkCompletionAdmissionOwner CompletionAdmission =>
        GetOrStartState().CompletionAdmission;

    internal ZLinkInboundDispatchStatus SnapshotInboundDispatch()
    {
        var snapshot = Volatile.Read(ref _state)?.InboundDispatchBudget.Snapshot()
                       ?? new ZLinkInboundDispatchBudgetSnapshot(
                           Registration.InboundDispatchOptions
                               .EffectiveApplicationHwmBytes,
                           0,
                           0,
                           0,
                           false);
        var completion = Volatile.Read(ref _state)?
            .CompletionAdmission.Snapshot();
        return new ZLinkInboundDispatchStatus(
            snapshot.ApplicationHwmBytes,
            snapshot.PendingPayloadBytes,
            snapshot.QueuedPayloadBytes,
            snapshot.ActivePayloadBytes,
            snapshot.ApplicationReceivePaused,
            PendingCompletionSends: checked((ulong)(
                completion?.PendingCompletionSends ?? 0)),
            CompletionSendLimit: checked((ulong)(
                completion?.CompletionSendLimit ?? 0)));
    }

    internal void RunDetached(
        string name,
        Func<CancellationToken, ValueTask> callback)
    {
        _ = TryRunDetached(name, callback);
    }

    internal bool TryRunDetached(
        string name,
        Func<CancellationToken, ValueTask> callback)
    {
        var state = AmbientOperation.Value is { IsActive: true } operation
                    && ReferenceEquals(operation.Runtime, this)
            ? operation.State
            : Volatile.Read(ref _executionScope) is { } executionScope
              && ZLinkRuntimeTaskRunner.IsCurrentExecutionFor(executionScope)
                ? _state
            : IsStarted
                ? _state
                : null;
        return state is not null && state.TaskRunner.TryRunDetached(name, callback);
    }

    internal ValueTask<ZLinkFrameworkComponentState> GetStartedStateForRoutingAsync(
        CancellationToken cancellationToken)
    {
        return GetStartedStateAsync(cancellationToken);
    }

    internal RoutingId PrepareLocationNodeRoutingId()
    {
        var registration = Registration.SpotNodes.Values.FirstOrDefault();
        return registration is null
            ? RoutingId.From(Guid.NewGuid().ToString("n"))
            : ZLinkSpotNodeInitializer.PrepareNodeRoutingId(registration);
    }

    public async ValueTask StartAsync(CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken);
        try
        {
            if (Volatile.Read(ref _lifecyclePhase) == (int)ZLinkRuntimeLifecyclePhase.Running) return;

            Volatile.Write(ref _lifecyclePhase, (int)ZLinkRuntimeLifecyclePhase.Starting);
            try
            {
                _state = await _stateFactory.CreateAsync().ConfigureAwait(false);
                await RecoverPublishedRelocationsAsync(cancellationToken)
                    .ConfigureAwait(false);
                lock (_operationGate) _acceptingOperations = true;
                Volatile.Write(ref _lifecyclePhase, (int)ZLinkRuntimeLifecyclePhase.Running);
                _locationLifecycle?.ResumeBackgroundWork();
            }
            catch (Exception startFailure)
            {
                Volatile.Write(ref _lifecyclePhase, (int)ZLinkRuntimeLifecyclePhase.Stopping);
                await StopAcceptingOperationsAsync().ConfigureAwait(false);
                var failures = await CleanupRuntimeGenerationAsync(_state).ConfigureAwait(false);
                _state = null;
                Interlocked.Exchange(ref _executionScope, null);
                Volatile.Write(ref _lifecyclePhase, (int)ZLinkRuntimeLifecyclePhase.Stopped);
                ThrowCleanupFailures(failures, startFailure);
            }
        }
        finally
        {
            _gate.Release();
        }
    }

    public async ValueTask StopAsync(CancellationToken cancellationToken)
    {
        ThrowIfStopRequestedFromOwnedWork();
        await _gate.WaitAsync(cancellationToken);
        try
        {
            ThrowIfStopRequestedFromOwnedWork();
            if ((ZLinkRuntimeLifecyclePhase)Volatile.Read(ref _lifecyclePhase)
                == ZLinkRuntimeLifecyclePhase.Stopped)
                return;
            var stateToDispose = _state;
            Volatile.Write(ref _lifecyclePhase, (int)ZLinkRuntimeLifecyclePhase.Stopping);
            try
            {
                var operationsDrained = StopAcceptingOperationsAsync();
                stateToDispose?.CancelActiveSpotOperations();
                await operationsDrained.ConfigureAwait(false);
                var failures = await CleanupRuntimeGenerationAsync(stateToDispose).ConfigureAwait(false);
                ThrowCleanupFailures(failures);
            }
            finally
            {
                _state = null;
                Interlocked.Exchange(ref _executionScope, null);
                Volatile.Write(ref _lifecyclePhase, (int)ZLinkRuntimeLifecyclePhase.Stopped);
            }
        }
        finally
        {
            _gate.Release();
        }
    }

    /// <summary>
    /// Stops a runtime after the drain deadline has already expired. Unlike
    /// the normal stop path, this does not wait for active operations before
    /// disposing their owners: stream-session and worker disposal cancel the
    /// corresponding execution tokens and bound their cleanup.
    /// </summary>
    internal async ValueTask ForceStopAsync(CancellationToken cancellationToken)
    {
        ThrowIfStopRequestedFromOwnedWork();
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ThrowIfStopRequestedFromOwnedWork();
            if ((ZLinkRuntimeLifecyclePhase)Volatile.Read(ref _lifecyclePhase)
                == ZLinkRuntimeLifecyclePhase.Stopped)
                return;

            var stateToDispose = _state;
            Volatile.Write(ref _lifecyclePhase, (int)ZLinkRuntimeLifecyclePhase.Stopping);
            try
            {
                lock (_operationGate) _acceptingOperations = false;
                stateToDispose?.FenceOperations();
                stateToDispose?.CancelActiveSpotOperations();
                stateToDispose?.ForceStopStreamSessions();
                var failures = await CleanupRuntimeGenerationAsync(
                        stateToDispose,
                        cancellationToken)
                    .ConfigureAwait(false);
                if (cancellationToken.IsCancellationRequested
                    && failures.Count > 0)
                    throw new OperationCanceledException(
                        "Framework force-stop exceeded its cleanup deadline.",
                        failures.Count == 1
                            ? failures[0]
                            : new AggregateException(failures),
                        cancellationToken);
                ThrowCleanupFailures(failures);
            }
            finally
            {
                _state = null;
                Interlocked.Exchange(ref _executionScope, null);
                Volatile.Write(ref _lifecyclePhase, (int)ZLinkRuntimeLifecyclePhase.Stopped);
            }
        }
        finally
        {
            _gate.Release();
        }
    }

    private void ThrowIfStopRequestedFromOwnedWork()
    {
        if (Volatile.Read(ref _executionScope) is { } executionScope
            && ZLinkRuntimeTaskRunner.IsCurrentExecutionFor(executionScope))
            throw new InvalidOperationException(
                "The framework runtime cannot stop from one of its own managed tasks. Request shutdown from an external lifecycle owner.");
        if (AmbientOperation.Value is { IsActive: true } operation
            && ReferenceEquals(operation.Runtime, this))
            throw new InvalidOperationException(
                "The framework runtime cannot stop from one of its active operations. Request shutdown from an external lifecycle owner.");
    }

    private async ValueTask<List<Exception>> CleanupRuntimeGenerationAsync(
        ZLinkFrameworkComponentState? state,
        CancellationToken forceStopToken = default)
    {
        var failures = new List<Exception>();
        ZLinkWorkerPool? workerPool;
        lock (_workerPoolGate)
        {
            workerPool = _workerPool;
            _workerPool = null;
        }

        if (workerPool is not null) Capture(workerPool.RequestStop);
        if (_locationLifecycle is not null)
            await CaptureAsync(_locationLifecycle.PauseBackgroundWorkAsync).ConfigureAwait(false);
        var generationCleanupReporter =
            (state?.ErrorSink ?? Volatile.Read(ref _generationErrorSink))
            ?.CaptureGenerationReporter();
        await CaptureAsync(
                () => ResetActorRuntimeGenerationAsync(
                    forceStopToken,
                    generationCleanupReporter))
            .ConfigureAwait(false);
        if (state is not null)
            await CaptureAsync(forceStopToken.CanBeCanceled
                    ? () => state.ForceStopAsync(forceStopToken)
                    : state.DisposeAsync)
                .ConfigureAwait(false);
        Capture(_standaloneActorRelocationRuntime.ReleaseRetainedPermits);
        if (state is not null) DetachErrorSink(state.ErrorSink);
        if (_locationLifecycle is not null)
            await CaptureAsync(_locationLifecycle.PauseBackgroundWorkAsync).ConfigureAwait(false);

        if (workerPool is not null) await CaptureAsync(workerPool.DisposeAsync).ConfigureAwait(false);
        return failures;

        async ValueTask CaptureAsync(Func<ValueTask> cleanup)
        {
            try
            {
                await cleanup().ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }

        void Capture(Action cleanup)
        {
            try
            {
                cleanup();
            }
            catch (Exception exception)
            {
                failures.Add(exception);
            }
        }
    }

    private static void ThrowCleanupFailures(
        IReadOnlyList<Exception> cleanupFailures,
        Exception? primaryFailure = null)
    {
        if (primaryFailure is null && cleanupFailures.Count == 0) return;
        if (primaryFailure is not null && cleanupFailures.Count == 0)
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(primaryFailure).Throw();
        if (primaryFailure is null && cleanupFailures.Count == 1)
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(cleanupFailures[0]).Throw();

        throw new AggregateException(
            primaryFailure is null
                ? cleanupFailures
                : new[] { primaryFailure }.Concat(cleanupFailures));
    }

    private async ValueTask<ZLinkFrameworkComponentState> GetStartedStateAsync(
        CancellationToken cancellationToken)
    {
        var phase = (ZLinkRuntimeLifecyclePhase)Volatile.Read(ref _lifecyclePhase);
        if (phase is ZLinkRuntimeLifecyclePhase.Stopping or ZLinkRuntimeLifecyclePhase.Starting)
            throw new InvalidOperationException($"ZLink framework runtime is {phase.ToString().ToLowerInvariant()}.");
        if (phase == ZLinkRuntimeLifecyclePhase.Stopped) await StartAsync(cancellationToken);

        return IsStarted && _state is { } state
            ? state
            : throw new InvalidOperationException("ZLink framework runtime is not started.");
    }

    private ZLinkFrameworkComponentState GetOrStartState()
    {
        if (!IsStarted || _state is null)
            throw new InvalidOperationException(
                "ZLink framework runtime is not started. Call StartAsync before using synchronous runtime APIs.");

        return _state;
    }

    private Task StopAcceptingOperationsAsync()
    {
        lock (_operationGate)
        {
            _acceptingOperations = false;
            if (_activeOperations == 0) return Task.CompletedTask;
            return (_operationsDrained ??= new TaskCompletionSource(
                TaskCreationOptions.RunContinuationsAsynchronously)).Task;
        }
    }

    private ZLinkRuntimeOperationLease EnterOperationUnderLock(bool countAsRequest)
    {
        if (!_acceptingOperations
            || Volatile.Read(ref _lifecyclePhase) != (int)ZLinkRuntimeLifecyclePhase.Running
            || _state is not { } state
            || (_locationRuntime is not null
                && !_locationRuntime.IsOwnerAdmissionOpen))
            throw new InvalidOperationException("ZLink framework runtime is not accepting operations.");
        _activeOperations++;
        if (countAsRequest) _activeRequests++;
        var previous = AmbientOperation.Value;
        var ownership = new ZLinkRuntimeOperationOwnership(this, state);
        AmbientOperation.Value = ownership;
        return new ZLinkRuntimeOperationLease(this, ownership, previous, countAsRequest);
    }

    private bool IsAmbientOperationCurrent(
        ZLinkRuntimeOperationOwnership ownership) =>
        !ownership.State.IsOperationFenced
        && ReferenceEquals(ownership.State, Volatile.Read(ref _state));

    private void EnsureAmbientOperationCurrent(
        ZLinkRuntimeOperationOwnership ownership)
    {
        if (!IsAmbientOperationCurrent(ownership))
            throw new InvalidOperationException(
                "The framework runtime operation belongs to a stopped generation.");
    }

    private void ExitOperation(bool countsOperation, bool countsRequest)
    {
        TaskCompletionSource? drained = null;
        lock (_operationGate)
        {
            if (countsRequest && --_activeRequests < 0)
                throw new InvalidOperationException("Runtime request lease count became negative.");
            if (countsOperation && --_activeOperations < 0)
                throw new InvalidOperationException("Runtime operation lease count became negative.");
            if (_activeOperations == 0 && !_acceptingOperations)
            {
                drained = _operationsDrained;
                _operationsDrained = null;
            }
        }
        drained?.TrySetResult();
    }

    internal sealed class ZLinkRuntimeOperationLease : IDisposable
    {
        private readonly ZLinkFrameworkRuntime? _runtime;
        private readonly ZLinkRuntimeOperationOwnership? _ownership;
        private readonly ZLinkRuntimeOperationOwnership? _previous;
        private readonly bool _countsOperation;
        private readonly bool _countsRequest;
        private int _disposed;

        internal ZLinkRuntimeOperationLease()
        {
        }

        internal ZLinkRuntimeOperationLease(
            ZLinkFrameworkRuntime runtime,
            bool countsRequest)
            : this(runtime, countsOperation: false, countsRequest)
        {
        }

        internal ZLinkRuntimeOperationLease(
            ZLinkFrameworkRuntime runtime,
            bool countsOperation,
            bool countsRequest)
        {
            _runtime = runtime;
            _countsOperation = countsOperation;
            _countsRequest = countsRequest;
        }

        internal ZLinkRuntimeOperationLease(
            ZLinkFrameworkRuntime runtime,
            ZLinkRuntimeOperationOwnership ownership,
            ZLinkRuntimeOperationOwnership? previous,
            bool countsRequest)
        {
            _runtime = runtime;
            _ownership = ownership;
            _previous = previous;
            _countsOperation = true;
            _countsRequest = countsRequest;
        }

        public void Dispose()
        {
            if (Interlocked.Exchange(ref _disposed, 1) == 0)
            {
                _ownership?.Deactivate();
                if (_ownership is not null && ReferenceEquals(AmbientOperation.Value, _ownership))
                    AmbientOperation.Value = _previous;
                _runtime?.ExitOperation(_countsOperation, _countsRequest);
            }
        }
    }

    internal sealed class ZLinkRuntimeOperationOwnership(
        ZLinkFrameworkRuntime runtime,
        ZLinkFrameworkComponentState state)
    {
        private int _active = 1;

        public ZLinkFrameworkRuntime Runtime { get; } = runtime;

        public ZLinkFrameworkComponentState State { get; } = state;

        public bool IsActive => Volatile.Read(ref _active) != 0;

        public void Deactivate() => Interlocked.Exchange(ref _active, 0);
    }
}

internal enum ZLinkRuntimeLifecyclePhase
{
    Stopped,
    Running,
    Stopping,
    Starting
}
