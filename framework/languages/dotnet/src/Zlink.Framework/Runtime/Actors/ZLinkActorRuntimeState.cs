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

    public bool IsDispatchBlocked => _destroyPhase != ZLinkActorDestroyPhase.None || _teardownPending;

    public bool IsTeardownPending => _teardownPending;

    public ZLinkSpotActivation? LiveActivation
        => Activation is { IsDisposed: false } activation ? activation : null;

    public RoutingId? SpotRid => LiveActivation?.SpotRid;

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
        Activation = activation;
    }

    public void LeaveSpotIfCurrent(ZLinkSpotActivation activation)
    {
        if (ReferenceEquals(Activation, activation)) Activation = null;
    }

    public void BindNativeActorRef(ZLinkBackendActorRef actorRef)
    {
        EnsureReusable();
        NativeActorRef = actorRef;
    }

    public bool BindActorInstance(IZLinkActor actor)
    {
        EnsureReusable();
        if (!string.Equals(ActorId, actor.ActorId, StringComparison.Ordinal))
            throw new InvalidOperationException(
                $"Actor state id '{ActorId}' does not match actor id '{actor.ActorId}'.");

        if (Actor is not null
            && !ReferenceEquals(Actor, actor)
            && (SessionId is not null || Activation is not null))
            throw new InvalidOperationException(
                $"Actor id '{actor.ActorId}' is already bound to another actor instance.");

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
        string bindingToken)
    {
        EnsureReusable();
        if (bindingToken.Length == 0)
            throw new InvalidOperationException("Actor session binding token must not be empty.");

        lock (_sessionGate)
        {
            _boundSession = new ZLinkActorBoundSession(sessionNodeRid, sessionRid, bindingToken);
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
            throw new InvalidOperationException(
                $"Actor context for '{ActorId}' is no longer valid because actor ownership moved to another SpotNode.");
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

    private void EnsureDispatchAvailable()
    {
        if (!IsDispatchBlocked && !Handoff.BlocksLocalDispatch) return;

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            $"Actor '{ActorId}' is not available while its lifecycle transition is active.");
    }

    private void EnsureReusable()
    {
        if (!IsDispatchBlocked) return;

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

internal readonly record struct ZLinkActorBoundSession(
    RoutingId? SessionNodeRid,
    RoutingId SessionRid,
    string BindingToken);

internal readonly record struct ZLinkActorCreationOperation(
    Task<IZLinkActor> Task,
    bool Created);

internal readonly record struct ZLinkActorPlacementSelection(
    ZLinkSpotActivation? Activation,
    bool Prune);
