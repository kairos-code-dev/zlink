namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorRuntimeState(
    string actorId,
    TimeProvider? timeProvider = null,
    Action<string>? handoffDiagnostic = null)
{
    private static readonly AsyncLocal<DispatchOwnership?> AmbientDispatch = new();
    private readonly ZLinkActorDispatchMailbox _dispatchMailbox = new();
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly object _sessionGate = new();
    private Task<IZLinkActor>? _actorCreationTask;
    private int _actorMetricActive;
    private ZLinkActorBoundSession? _boundSession;
    private ZLinkPendingActorSessionRoute? _pendingSessionRoute;
    private TaskCompletionSource<Exception?>? _teardownAttempt;

    public string ActorId { get; } = actorId;

    public ZLinkActorHandoffState Handoff { get; } = new(
        actorId,
        timeProvider ?? TimeProvider.System,
        handoffDiagnostic);

    public string? ActorType { get; private set; }

    public string? SessionId { get; private set; }

    public IZLinkStream? Stream { get; private set; }

    public ZLinkBackendActorRef? NativeActorRef { get; private set; }

    public ZLinkBackendActorRef? RetiredLocalActorRef { get; private set; }

    public ZLinkSpotActivation? Activation { get; private set; }

    public ZLinkActorDispatchState? CurrentDispatch { get; private set; }

    public ZLinkActorContext? Context { get; private set; }

    public IZLinkActor? Actor { get; private set; }

    public bool IsConfigured { get; private set; }

    public bool ContextInvalidated { get; private set; }

    private volatile ZLinkActorDestroyPhase _destroyPhase;
    private volatile bool _teardownPending;
    private volatile bool _reservedCreationPending;
    private int _deferredJoinPending;

    public bool IsDispatchBlocked =>
        _destroyPhase != ZLinkActorDestroyPhase.None
        || _teardownPending
        || _reservedCreationPending;

    public bool IsTeardownPending => _teardownPending;

    public ZLinkActorDispatchMailbox.BarrierReservation ReserveDeferredJoinBarrier()
    {
        if (IsDispatchBlocked
            || Handoff.BlocksLocalDispatch
            || Interlocked.CompareExchange(ref _deferredJoinPending, 1, 0) != 0)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorMoving,
                $"Actor '{ActorId}' already has an active lifecycle transition.");

        try
        {
            return _dispatchMailbox.ReserveBarrier();
        }
        catch
        {
            Volatile.Write(ref _deferredJoinPending, 0);
            throw;
        }
    }

    public void EnsureDeferredJoinIdentity(IZLinkActor actor, ulong objectGeneration)
    {
        if (ContextInvalidated
            || !ReferenceEquals(Actor, actor)
            || NativeActorRef is not { Generation: var currentGeneration }
            || currentGeneration != objectGeneration)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorGenerationStale,
                $"Actor '{ActorId}' no longer matches the context that registered the Join.");
    }

    public void ReleaseDeferredJoinBarrier()
    {
        Volatile.Write(ref _deferredJoinPending, 0);
    }

    public void BeginReservedCreation()
    {
        EnsureReusable();
        _reservedCreationPending = true;
    }

    public void PublishReservedCreation()
    {
        _reservedCreationPending = false;
    }

    public ZLinkSpotActivation? LiveActivation
        => Activation is { IsDisposed: false } activation ? activation : null;

    public string? SpotId => LiveActivation?.SpotId;

    public void AttachStream(IZLinkStream stream)
    {
        SessionId = stream.SessionId;
        Stream = stream;
    }

    public bool DetachStreamIfCurrent(IZLinkStream stream)
    {
        if (!string.Equals(SessionId, stream.SessionId, StringComparison.Ordinal)) return false;

        SessionId = null;
        Stream = null;
        return true;
    }

    public void JoinSpot(ZLinkSpotActivation activation)
    {
        Context?.UpdateSameNodeSpot(activation.SpotId);
        Activation = activation;
    }

    public void LeaveSpotIfCurrent(ZLinkSpotActivation activation)
    {
        if (!ReferenceEquals(Activation, activation)) return;

        Context?.UpdateSameNodeSpot(null);
        Activation = null;
    }

    public void BindNativeActorRef(ZLinkBackendActorRef actorRef)
    {
        EnsureReusable();
        NativeActorRef = actorRef;
    }

    public bool BindActorInstance(IZLinkActor actor)
    {
        EnsureReusable();
        if (Context is { } expectedContext
            && !ReferenceEquals(actor.Context, expectedContext))
            throw new InvalidOperationException(
                $"Actor '{ActorId}' must expose its framework-issued Context.");

        if (Actor is not null
            && !ReferenceEquals(Actor, actor)
            && (SessionId is not null || Activation is not null))
            throw new InvalidOperationException(
                $"Actor id '{ActorId}' is already bound to another actor instance.");

        if (ReferenceEquals(Actor, actor)) return false;

        Actor = actor;
        if (Interlocked.Exchange(ref _actorMetricActive, 1) == 0)
            ZLinkRuntimeMetrics.RecordActorCreated();
        IsConfigured = false;
        return true;
    }

    public ZLinkActorContext GetOrCreateContext(Func<ZLinkActorContext> createContext)
    {
        EnsureReusable();
        if (Context is not null) return Context;

        ContextInvalidated = false;
        return Context = createContext();
    }

    public bool TryBeginActorConfiguration()
    {
        if (IsConfigured) return false;

        IsConfigured = true;
        return true;
    }

    public void RollBackActorConfiguration(bool clearAssignedActor)
    {
        IsConfigured = false;
        if (clearAssignedActor)
        {
            Actor = null;
            ClearActorMetric();
        }
    }

    public void BindSession(
        RoutingId? sessionNodeRid,
        RoutingId sessionRid,
        string bindingToken,
        ulong bindingGeneration = 1,
        ulong objectGeneration = 0,
        ulong authorityOwnerGeneration = 0,
        string meshName = "",
        ulong targetNodeGeneration = 1,
        ulong ownerLeaseGeneration = 0,
        ulong sessionOwnerNodeGeneration = 1,
        ulong acceptedHighWater = 0)
    {
        EnsureReusable();
        if (bindingToken.Length == 0)
            throw new InvalidOperationException("Actor session binding token must not be empty.");
        if (string.IsNullOrWhiteSpace(meshName)
            || targetNodeGeneration == 0
            || ownerLeaseGeneration == 0)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorLocationStale,
                $"Actor '{ActorId}' session binding requires an exact Mesh, node lifecycle, and owner lease.");

        lock (_sessionGate)
        {
            _boundSession = new ZLinkActorBoundSession(
                sessionNodeRid,
                sessionRid,
                bindingToken,
                bindingGeneration,
                objectGeneration,
                authorityOwnerGeneration,
                meshName,
                targetNodeGeneration,
                ownerLeaseGeneration,
                sessionOwnerNodeGeneration,
                acceptedHighWater);
        }
    }

    public void RecordBoundSessionAccepted(string bindingToken)
    {
        lock (_sessionGate)
        {
            if (_boundSession is not { } current
                || !string.Equals(
                    current.BindingToken,
                    bindingToken,
                    StringComparison.Ordinal))
                return;
            _boundSession = current with
            {
                AcceptedHighWater = checked(current.AcceptedHighWater + 1)
            };
        }
    }

    public void RecordRelocatedSessionAccepted(RoutingId sessionRid)
    {
        lock (_sessionGate)
        {
            if (_pendingSessionRoute is { } pending
                && pending.Route.SessionRid is { } pendingSessionRid
                && pendingSessionRid == sessionRid)
            {
                _pendingSessionRoute = pending with
                {
                    Route = pending.Route with
                    {
                        AcceptedHighWater =
                            checked(pending.Route.AcceptedHighWater + 1)
                    }
                };
                return;
            }

            if (_boundSession is not { } current
                || current.SessionRid != sessionRid)
                return;
            _boundSession = current with
            {
                AcceptedHighWater = checked(current.AcceptedHighWater + 1)
            };
        }
    }

    public void StageRelocationSessionRoute(
        string handoffId,
        ZLinkRemoteActorBoundSessionRoute route)
    {
        lock (_sessionGate)
        {
            if (!route.IsBound)
            {
                _pendingSessionRoute = null;
                return;
            }
            if (_pendingSessionRoute is { } pending
                && !string.Equals(
                    pending.HandoffId,
                    handoffId,
                    StringComparison.Ordinal))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorMoving,
                    $"Actor '{ActorId}' already stages another session route.");
            _pendingSessionRoute = new ZLinkPendingActorSessionRoute(
                handoffId,
                route,
                TargetActor: null,
                TargetAuthorityOwnerGeneration: 0);
        }
    }

    public void MarkRelocationSessionAuthorityCommitted(
        string handoffId,
        ZLinkBackendActorRef targetActor,
        ulong targetAuthorityOwnerGeneration,
        string targetMeshName,
        ulong targetNodeGeneration,
        ulong targetOwnerLeaseGeneration)
    {
        lock (_sessionGate)
        {
            if (_pendingSessionRoute is not { } pending)
                return;
            if (!string.Equals(pending.HandoffId, handoffId, StringComparison.Ordinal)
                || pending.Route.ObjectGeneration != targetActor.Generation
                || string.IsNullOrWhiteSpace(targetMeshName)
                || targetNodeGeneration == 0
                || targetOwnerLeaseGeneration == 0
                || targetAuthorityOwnerGeneration
                <= pending.Route.AuthorityOwnerGeneration)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorGenerationStale,
                    $"Actor '{ActorId}' staged session route does not match committed authority.");
            _pendingSessionRoute = pending with
            {
                TargetActor = targetActor,
                TargetAuthorityOwnerGeneration = targetAuthorityOwnerGeneration,
                TargetMeshName = targetMeshName,
                TargetNodeGeneration = targetNodeGeneration,
                TargetOwnerLeaseGeneration = targetOwnerLeaseGeneration
            };
        }
    }

    public bool TryGetCommittedRelocationSessionRoute(
        string handoffId,
        out ZLinkPendingActorSessionRoute route)
    {
        lock (_sessionGate)
        {
            if (_pendingSessionRoute is
                {
                    TargetActor: not null,
                    TargetAuthorityOwnerGeneration: > 0
                } pending
                && string.Equals(
                    pending.HandoffId,
                    handoffId,
                    StringComparison.Ordinal))
            {
                route = pending;
                return true;
            }
        }

        route = default;
        return false;
    }

    public bool TryGetStagedRelocationSessionRoute(
        string handoffId,
        out ZLinkRemoteActorBoundSessionRoute route)
    {
        lock (_sessionGate)
        {
            if (_pendingSessionRoute is { } pending
                && string.Equals(
                    pending.HandoffId,
                    handoffId,
                    StringComparison.Ordinal))
            {
                route = pending.Route;
                return true;
            }
        }
        route = default;
        return false;
    }

    public void CompleteRelocationSessionRoute(string handoffId)
    {
        lock (_sessionGate)
        {
            if (_pendingSessionRoute is not { } pending
                || !string.Equals(
                    pending.HandoffId,
                    handoffId,
                    StringComparison.Ordinal))
                return;
            var target = pending.TargetActor
                         ?? throw new InvalidOperationException(
                             "A session route cannot complete before authority commit.");
            var route = pending.Route;
            _boundSession = new ZLinkActorBoundSession(
                route.NodeRid,
                route.SessionRid!.Value,
                route.BindingToken!,
                route.BindingGeneration,
                target.Generation,
                pending.TargetAuthorityOwnerGeneration,
                pending.TargetMeshName!,
                pending.TargetNodeGeneration,
                pending.TargetOwnerLeaseGeneration,
                route.SessionOwnerNodeGeneration,
                route.AcceptedHighWater);
            _pendingSessionRoute = null;
        }
    }

    public void AbortRelocationSessionRoute(string handoffId)
    {
        lock (_sessionGate)
        {
            if (_pendingSessionRoute is { } pending
                && string.Equals(
                    pending.HandoffId,
                    handoffId,
                    StringComparison.Ordinal))
                _pendingSessionRoute = null;
        }
    }

    public void UnbindSession(string bindingToken)
    {
        if (bindingToken.Length == 0) return;

        lock (_sessionGate)
        {
            if (_boundSession is { BindingToken: var current }
                && string.Equals(current, bindingToken, StringComparison.Ordinal))
                _boundSession = null;
        }
    }

    public bool TryGetBoundSession(out ZLinkActorBoundSession session)
    {
        lock (_sessionGate)
        {
            if (_boundSession is { } current)
            {
                session = current;
                return true;
            }
        }

        session = default;
        return false;
    }

    public bool TryUseBoundSession(
        string expectedBindingToken,
        Func<ZLinkActorBoundSession, bool> operation)
    {
        ArgumentNullException.ThrowIfNull(operation);

        lock (_sessionGate)
        {
            if (_boundSession is not { } current
                || !string.Equals(current.BindingToken, expectedBindingToken, StringComparison.Ordinal))
                return true;

            return operation(current);
        }
    }

    public ZLinkActorBoundSession? ClearAfterDestroy()
    {
        return TransitionLocalInstance(ZLinkActorTerminalTransition.Destroyed, null);
    }

    public void InvalidateRuntimeGeneration()
    {
        var failure = new InvalidOperationException(
            $"Actor '{ActorId}' belongs to a stopped framework runtime generation.");
        var teardownAttempt = _teardownAttempt;
        Handoff.AbortRuntimeGeneration(failure);
        ClearAfterDestroy();
        teardownAttempt?.TrySetResult(failure);
    }

    public void RetireMigratedActorInstance(ZLinkBackendActorRef sourceActor)
    {
        _ = TransitionLocalInstance(
            ZLinkActorTerminalTransition.Migrated,
            sourceActor);
    }

    private ZLinkActorBoundSession? TransitionLocalInstance(
        ZLinkActorTerminalTransition transition,
        ZLinkBackendActorRef? retiredLocalActor)
    {
        ZLinkActorBoundSession? releasedBoundSession = null;
        if (transition == ZLinkActorTerminalTransition.Destroyed)
            lock (_sessionGate)
            {
                releasedBoundSession = _boundSession;
                _boundSession = null;
            }

        SessionId = null;
        Stream = null;
        Activation = null;
        CurrentDispatch = null;
        Context = null;
        Actor = null;
        ClearActorMetric();
        ActorType = null;
        IsConfigured = false;
        ContextInvalidated = true;
        _actorCreationTask = null;

        switch (transition)
        {
            case ZLinkActorTerminalTransition.Destroyed:
                NativeActorRef = null;
                RetiredLocalActorRef = null;
                _destroyPhase = ZLinkActorDestroyPhase.None;
                _teardownPending = false;
                _teardownAttempt = null;
                Handoff.Reset();
                _pendingSessionRoute = null;
                break;
            case ZLinkActorTerminalTransition.Migrated:
                RetiredLocalActorRef = retiredLocalActor
                    ?? throw new ArgumentNullException(nameof(retiredLocalActor));
                Handoff.CompleteSourceMigration();
                break;
            default:
                throw new InvalidOperationException("Unknown actor terminal transition.");
        }

        return releasedBoundSession;
    }

    private void ClearActorMetric()
    {
        if (Interlocked.Exchange(ref _actorMetricActive, 0) != 0)
            ZLinkRuntimeMetrics.RecordActorClosed();
    }

    private enum ZLinkActorTerminalTransition
    {
        Destroyed,
        Migrated
    }

    public void PrepareForTransferredActivation()
    {
        if (Actor is not null || IsConfigured)
            throw new InvalidOperationException(
                $"Actor '{ActorId}' already has an active local instance.");

        NativeActorRef = null;
        ContextInvalidated = false;
    }

    public void ClearRetiredLocalActorRef(ZLinkBackendActorRef actor)
    {
        if (RetiredLocalActorRef == actor) RetiredLocalActorRef = null;
    }

    public void BeginTeardown()
    {
        _teardownPending = true;
        ContextInvalidated = true;
    }

    public ZLinkActorTeardownOperation BeginOrJoinTeardownAttempt()
    {
        if (!_teardownPending)
            throw new InvalidOperationException(
                $"Actor '{ActorId}' does not have a pending teardown.");
        if (_teardownAttempt is { } existing)
            return new ZLinkActorTeardownOperation(existing.Task, false, false);

        var nativeAlreadyDestroyed = false;
        switch (_destroyPhase)
        {
            case ZLinkActorDestroyPhase.None:
                _destroyPhase = ZLinkActorDestroyPhase.DestroyingNative;
                break;
            case ZLinkActorDestroyPhase.NativeDestroyed:
                _destroyPhase = ZLinkActorDestroyPhase.ReleasingOwnership;
                nativeAlreadyDestroyed = true;
                break;
            default:
                throw new InvalidOperationException(
                    $"Actor '{ActorId}' teardown phase has no owning operation.");
        }

        _teardownAttempt = new TaskCompletionSource<Exception?>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        return new ZLinkActorTeardownOperation(
            _teardownAttempt.Task,
            true,
            nativeAlreadyDestroyed);
    }

    public void MarkNativeDestroyed(ZLinkActorTeardownOperation operation)
    {
        EnsureCurrentTeardownAttempt(operation);
        if (_destroyPhase != ZLinkActorDestroyPhase.DestroyingNative)
            throw new InvalidOperationException($"Actor '{ActorId}' native destroy is not active.");
        _destroyPhase = ZLinkActorDestroyPhase.ReleasingOwnership;
    }

    public void FailTeardownAttempt(
        ZLinkActorTeardownOperation operation,
        bool nativeDestroyed,
        Exception failure)
    {
        EnsureCurrentTeardownAttempt(operation);
        if (nativeDestroyed)
            _destroyPhase = ZLinkActorDestroyPhase.NativeDestroyed;
        else
            _destroyPhase = ZLinkActorDestroyPhase.None;
        var completion = _teardownAttempt!;
        _teardownAttempt = null;
        completion.TrySetResult(failure);
    }

    public ZLinkActorBoundSession? CompleteTeardownAttempt(ZLinkActorTeardownOperation operation)
    {
        EnsureCurrentTeardownAttempt(operation);
        var completion = _teardownAttempt!;
        var boundSession = ClearAfterDestroy();
        completion.TrySetResult(null);
        return boundSession;
    }

    private void EnsureCurrentTeardownAttempt(ZLinkActorTeardownOperation operation)
    {
        if (_teardownAttempt is null || !ReferenceEquals(_teardownAttempt.Task, operation.Completion))
            throw new InvalidOperationException(
                $"Actor '{ActorId}' teardown operation is no longer current.");
    }

    public void EnsureContextValid()
    {
        if (ContextInvalidated)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorMoving,
                $"Actor context for '{ActorId}' cannot start an operation after owner cutover.");
    }

    public void InvalidateContext()
    {
        ContextInvalidated = true;
        Activation = null;
    }

    public ZLinkActorPlacementSelection SelectPlacementLocked(bool pruneWhenSessionless)
    {
        var currentActivation = Activation;
        var prune = false;
        if (currentActivation is not null && currentActivation.IsDisposed)
        {
            Activation = null;
            currentActivation = null;
            prune = pruneWhenSessionless && SessionId is null;
        }

        return new ZLinkActorPlacementSelection(currentActivation, prune);
    }

    public async ValueTask<ZLinkActorCreationOperation> GetOrStartActorCreationAsync(
        string actorType,
        bool failIfExists,
        Func<Task<IZLinkActor>> createActor,
        CancellationToken cancellationToken)
    {
        var created = false;
        var task = await ExecuteLockedAsync(
            () =>
            {
                EnsureReusable();
                if (ActorType is not null
                    && !string.Equals(ActorType, actorType, StringComparison.Ordinal))
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorTypeMismatch,
                        $"Actor '{ActorId}' already uses actor type '{ActorType}', not '{actorType}'.");

                if (Actor is not null && ContextInvalidated)
                {
                    Actor = null;
                    Context = null;
                    NativeActorRef = null;
                    IsConfigured = false;
                    ContextInvalidated = false;
                }

                if (Actor is not null)
                {
                    if (failIfExists)
                        throw new ZLinkFrameworkException(
                            ZLinkFrameworkErrorKind.ActorAlreadyExists,
                            $"Actor '{ActorId}' already exists.");

                    return Task.FromResult(Actor);
                }

                if (_actorCreationTask is null)
                {
                    ActorType = actorType;
                    created = true;
                    _actorCreationTask = createActor();
                    _ = ClearActorCreationTaskWhenCompletedAsync(_actorCreationTask);
                }
                else if (failIfExists)
                {
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorAlreadyExists,
                        $"Actor '{ActorId}' is already being created.");
                }

                return _actorCreationTask;
            },
            cancellationToken).ConfigureAwait(false);

        return new ZLinkActorCreationOperation(task, created);
    }

    public async ValueTask ExecuteLockedAsync(
        Action operation,
        CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            operation();
        }
        finally
        {
            _gate.Release();
        }
    }

    public async ValueTask<T> ExecuteLockedAsync<T>(
        Func<T> operation,
        CancellationToken cancellationToken)
    {
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            return operation();
        }
        finally
        {
            _gate.Release();
        }
    }

    public async ValueTask<T> ExecuteLockedAsync<T>(
        Func<CancellationToken, ValueTask<T>> operation,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(operation);
        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            return await operation(cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            _gate.Release();
        }
    }

    public async ValueTask<T> ExecuteHandoffTransitionAsync<T>(
        Func<T> transition,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(transition);
        using var turn = await _dispatchMailbox.EnterAsync(cancellationToken).ConfigureAwait(false);
        return await ExecuteLockedAsync(
                () =>
                {
                    EnsureReusable();
                    return transition();
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<int> BeginHandoffCaptureAsync(CancellationToken cancellationToken)
    {
        if (AmbientDispatch.Value is { IsActive: true } ownership
            && ReferenceEquals(ownership.State, this))
        {
            return await ExecuteLockedAsync(
                    () =>
                    {
                        EnsureReusable();
                        var pendingRequests = _dispatchMailbox.PendingRequestCount;
                        Handoff.BeginCapture();
                        return pendingRequests;
                    },
                    cancellationToken)
                .ConfigureAwait(false);
        }

        using var turn = await _dispatchMailbox.EnterAsync(cancellationToken).ConfigureAwait(false);
        return await ExecuteLockedAsync(
                () =>
                {
                    EnsureReusable();
                    var pendingRequests = _dispatchMailbox.PendingRequestCount;
                    Handoff.BeginCapture();
                    return pendingRequests;
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask ExecuteDispatchAsync(
        ZlinkStreamHeader header,
        Func<CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        using var turn = await _dispatchMailbox.EnterAsync(
                cancellationToken,
                countAsPendingMessage: true)
            .ConfigureAwait(false);
        EnsureDispatchAvailable();
        using var dispatch = EnterDispatch(header);
        await operation(cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<T> ExecuteDispatchAsync<T>(
        ZlinkStreamHeader header,
        Func<CancellationToken, ValueTask<T>> operation,
        bool countAsPendingRequest,
        CancellationToken cancellationToken)
    {
        using var turn = await _dispatchMailbox.EnterAsync(
                cancellationToken,
                countAsPendingMessage: true,
                countAsPendingRequest)
            .ConfigureAwait(false);
        EnsureDispatchAvailable();
        using var dispatch = EnterDispatch(header);
        return await operation(cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask ExecuteLifecycleAsync(
        Func<CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        using var turn = await _dispatchMailbox.EnterAsync(cancellationToken).ConfigureAwait(false);
        EnsureDispatchAvailable();
        await operation(cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask ExecuteRelocationCompletionAsync(
        ulong objectGeneration,
        Func<CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        using var turn = await _dispatchMailbox.EnterAsync(cancellationToken)
            .ConfigureAwait(false);
        if (ContextInvalidated
            || NativeActorRef is not { Generation: var currentGeneration }
            || currentGeneration != objectGeneration
            || Actor is null)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorGenerationStale,
                $"Actor '{ActorId}' no longer matches its durable Join completion.");
        await operation(cancellationToken).ConfigureAwait(false);
    }

    private void EnsureDispatchAvailable()
    {
        if (!IsDispatchBlocked && !Handoff.BlocksLocalDispatch) return;

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            $"Actor '{ActorId}' is not available while its lifecycle transition is active.");
    }

    private void EnsureReusable()
    {
        // Reserved creation deliberately blocks public dispatch until the
        // authority Ready commit, but the factory, context and native Actor
        // binding that prepare that reservation must still mutate this state.
        if (_destroyPhase == ZLinkActorDestroyPhase.None && !_teardownPending)
            return;

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            $"Actor '{ActorId}' cannot be reused while teardown is incomplete.");
    }

    public DispatchScope EnterDispatch(ZlinkStreamHeader header)
    {
        var previous = CurrentDispatch;
        var previousAmbient = AmbientDispatch.Value;
        var ownership = new DispatchOwnership(this);
        CurrentDispatch = new ZLinkActorDispatchState(header);
        AmbientDispatch.Value = ownership;
        return new DispatchScope(this, previous, previousAmbient, ownership);
    }

    public DispatchScope EnterDeferredJoinExecution()
    {
        var previousAmbient = AmbientDispatch.Value;
        var ownership = new DispatchOwnership(this);
        AmbientDispatch.Value = ownership;
        return new DispatchScope(
            this,
            CurrentDispatch,
            previousAmbient,
            ownership);
    }

    private async Task ClearActorCreationTaskWhenCompletedAsync(Task<IZLinkActor> creationTask)
    {
        var succeeded = true;
        try
        {
            await creationTask.ConfigureAwait(false);
        }
        catch
        {
            succeeded = false;
        }

        await ExecuteLockedAsync(
            () =>
            {
                if (ReferenceEquals(_actorCreationTask, creationTask))
                {
                    if (succeeded)
                        _actorCreationTask = null;
                    else
                        ClearFailedActorCreationLocked();
                }
            },
            CancellationToken.None).ConfigureAwait(false);
    }

    private void ClearFailedActorCreationLocked()
    {
        _actorCreationTask = null;
        if (_teardownPending) return;

        Actor = null;
        ActorType = null;
        Context = null;
        Activation = null;
        NativeActorRef = null;
        IsConfigured = false;
        ContextInvalidated = true;
    }

    public readonly struct DispatchScope : IDisposable
    {
        private readonly ZLinkActorRuntimeState? _state;
        private readonly ZLinkActorDispatchState? _previous;
        private readonly DispatchOwnership? _previousAmbient;
        private readonly DispatchOwnership? _ownership;

        internal DispatchScope(
            ZLinkActorRuntimeState state,
            ZLinkActorDispatchState? previous,
            DispatchOwnership? previousAmbient,
            DispatchOwnership ownership)
        {
            _state = state;
            _previous = previous;
            _previousAmbient = previousAmbient;
            _ownership = ownership;
        }

        public void Dispose()
        {
            if (_state is not null)
            {
                _ownership?.Deactivate();
                _state.CurrentDispatch = _previous;
                AmbientDispatch.Value = _previousAmbient;
            }
        }
    }

    internal sealed class DispatchOwnership(ZLinkActorRuntimeState state)
    {
        private int _active = 1;

        public ZLinkActorRuntimeState State { get; } = state;

        public bool IsActive => Volatile.Read(ref _active) != 0;

        public void Deactivate() => Interlocked.Exchange(ref _active, 0);
    }
}

internal enum ZLinkActorDestroyPhase
{
    None,
    DestroyingNative,
    NativeDestroyed,
    ReleasingOwnership
}

internal readonly record struct ZLinkActorTeardownOperation(
    Task<Exception?> Completion,
    bool OwnsExecution,
    bool NativeAlreadyDestroyed);

// Actor-side delivery projection reconstructed from binding and relocation
// payloads. It does not publish or replace the Session owner's binding route.
internal readonly record struct ZLinkActorBoundSession(
    RoutingId? SessionNodeRid,
    RoutingId SessionRid,
    string BindingToken,
    ulong BindingGeneration = 1,
    ulong ObjectGeneration = 0,
    ulong AuthorityOwnerGeneration = 0,
    string MeshName = "",
    ulong TargetNodeGeneration = 1,
    ulong OwnerLeaseGeneration = 0,
    ulong SessionOwnerNodeGeneration = 1,
    ulong AcceptedHighWater = 0);

internal readonly record struct ZLinkPendingActorSessionRoute(
    string HandoffId,
    ZLinkRemoteActorBoundSessionRoute Route,
    ZLinkBackendActorRef? TargetActor,
    ulong TargetAuthorityOwnerGeneration,
    string? TargetMeshName = null,
    ulong TargetNodeGeneration = 0,
    ulong TargetOwnerLeaseGeneration = 0);

internal readonly record struct ZLinkActorCreationOperation(
    Task<IZLinkActor> Task,
    bool Created);

internal readonly record struct ZLinkActorPlacementSelection(
    ZLinkSpotActivation? Activation,
    bool Prune);
