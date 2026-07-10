namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorRuntimeState(string actorId)
{
    private readonly ZLinkActorDispatchMailbox _dispatchMailbox = new();
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly object _sessionGate = new();
    private readonly object _handoffGate = new();
    private readonly List<ZLinkActorHandoffFrame> _handoffFrames = [];
    private Task<IZLinkActor>? _actorCreationTask;
    private ZLinkActorBoundSession? _boundSession;
    private CancellationTokenSource? _forwardingExpiry;
    private ZLinkActorForwardingMapping? _forwarding;
    private TaskCompletionSource? _handoffBarrier;
    private int _handoffBarrierArrivals;
    private int _handoffBarrierExpected = int.MaxValue;
    private bool _handoffCaptureActive;
    private long _handoffArrivalIndex;

    public string ActorId { get; } = actorId;

    public string? ActorType { get; private set; }

    public string? SessionId { get; private set; }

    public IZLinkStream? Stream { get; private set; }

    public ZLinkBackendActorRef? NativeActorRef { get; private set; }

    public ZLinkSpotActivation? Activation { get; private set; }

    public ZLinkActorDispatchState? CurrentDispatch { get; private set; }

    public ZLinkActorContext? Context { get; private set; }

    public IZLinkActor? Actor { get; private set; }

    public bool IsConfigured { get; private set; }

    public bool ContextInvalidated { get; private set; }

    public bool IsDestroying { get; private set; }

    public ZLinkSpotActivation? LiveActivation
        => Activation is { IsDisposed: false } activation ? activation : null;

    public RoutingId? SpotRid => LiveActivation?.SpotRid;

    public bool IsJoined => LiveActivation is not null;

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
        NativeActorRef = actorRef;
    }

    public void BeginHandoffCapture()
    {
        lock (_handoffGate)
        {
            _handoffCaptureActive = true;
            _handoffFrames.Clear();
            _handoffArrivalIndex = 0;
        }
    }

    public void PrepareHandoffBarrier(TimeSpan timeout)
    {
        TaskCompletionSource barrier;
        lock (_handoffGate)
        {
            _handoffBarrierArrivals = 0;
            _handoffBarrierExpected = int.MaxValue;
            barrier = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            _handoffBarrier = barrier;
        }

        _ = ExpireHandoffBarrierAsync(timeout, barrier);
    }

    public bool NotifyHandoffFrameArrived()
    {
        lock (_handoffGate)
        {
            if (_handoffBarrier is null) return false;

            _handoffBarrierArrivals++;
            if (_handoffBarrierArrivals >= _handoffBarrierExpected)
                _handoffBarrier?.TrySetResult();
            return true;
        }
    }

    public async ValueTask WaitForHandoffBarrierAsync(
        int expectedFrameCount,
        CancellationToken cancellationToken)
    {
        Task barrier;
        lock (_handoffGate)
        {
            var completion = _handoffBarrier
                             ?? throw new InvalidOperationException($"Actor '{ActorId}' handoff barrier was not prepared.");
            _handoffBarrierExpected = expectedFrameCount;
            if (_handoffBarrierArrivals >= expectedFrameCount) completion.TrySetResult();
            barrier = completion.Task;
        }

        await barrier.WaitAsync(cancellationToken).ConfigureAwait(false);
        lock (_handoffGate) _handoffBarrier = null;
    }

    private async Task ExpireHandoffBarrierAsync(TimeSpan timeout, TaskCompletionSource barrier)
    {
        await Task.Delay(timeout).ConfigureAwait(false);
        lock (_handoffGate)
        {
            if (!ReferenceEquals(_handoffBarrier, barrier)) return;

            _handoffBarrier = null;
            barrier.TrySetCanceled();
        }
    }

    public bool TryCaptureHandoffFrame(ZLinkSpotActorFrame frame)
    {
        lock (_handoffGate)
        {
            if (!_handoffCaptureActive) return false;

            _handoffFrames.Add(ZLinkActorHandoffFrames.Capture(frame, _handoffArrivalIndex++));
            ZLinkFrameworkDebugLog.SpotDiscovery(
                $"handoff_backlog actor={ActorId} arrival={_handoffArrivalIndex - 1}");
            return true;
        }
    }

    public void ImportHandoffFrames(IReadOnlyList<ZLinkActorHandoffFrame> frames)
    {
        lock (_handoffGate)
        {
            _handoffCaptureActive = true;
            _handoffFrames.Clear();
            _handoffArrivalIndex = 0;
            foreach (var frame in frames.OrderBy(static frame => frame.ArrivalIndex))
            {
                _handoffFrames.Add(frame with { ArrivalIndex = _handoffArrivalIndex++ });
                ZLinkFrameworkDebugLog.SpotDiscovery(
                    $"backlog_enqueued actor={ActorId} arrival={_handoffArrivalIndex - 1}");
            }
        }
    }

    public IReadOnlyList<ZLinkActorHandoffFrame> DrainHandoffFrames()
    {
        lock (_handoffGate)
        {
            var frames = _handoffFrames.ToArray();
            _handoffFrames.Clear();
            return frames;
        }
    }

    public IReadOnlyList<ZLinkActorHandoffFrame> CompleteHandoffCapture()
    {
        lock (_handoffGate)
        {
            _handoffCaptureActive = false;
            var frames = _handoffFrames.ToArray();
            _handoffFrames.Clear();
            return frames;
        }
    }

    public void CancelHandoffCapture()
    {
        lock (_handoffGate)
        {
            _handoffCaptureActive = false;
            _handoffFrames.Clear();
        }
    }

    public void InstallForwardingMapping(
        ZLinkBackendActorRef sourceActor,
        ZLinkBackendActorRef targetActor,
        TimeSpan window)
    {
        CancellationTokenSource expiry;
        lock (_handoffGate)
        {
            _forwardingExpiry?.Cancel();
            _forwardingExpiry?.Dispose();
            expiry = new CancellationTokenSource();
            _forwardingExpiry = expiry;
            _forwarding = new ZLinkActorForwardingMapping(
                sourceActor,
                targetActor,
                DateTimeOffset.UtcNow + window);
        }

        _ = EvictForwardingMappingAsync(window, expiry);
    }

    public ZLinkActorFrameRoute ResolveFrameRoute(
        ZLinkBackendActorRef frameActor,
        out ZLinkBackendActorRef targetActor)
    {
        lock (_handoffGate)
        {
            targetActor = NativeActorRef ?? frameActor;
            if (targetActor.NodeRid == frameActor.NodeRid
                && targetActor.Generation == frameActor.Generation)
                return ZLinkActorFrameRoute.Current;

            if (_forwarding is { } forwarding
                && forwarding.SourceActor.NodeRid == frameActor.NodeRid
                && forwarding.SourceActor.Generation == frameActor.Generation
                && forwarding.ExpiresAt > DateTimeOffset.UtcNow)
            {
                targetActor = forwarding.TargetActor;
                return ZLinkActorFrameRoute.Forward;
            }

            return ZLinkActorFrameRoute.Stale;
        }
    }

    private async Task EvictForwardingMappingAsync(TimeSpan window, CancellationTokenSource expiry)
    {
        try
        {
            await Task.Delay(window, expiry.Token).ConfigureAwait(false);
            lock (_handoffGate)
            {
                if (!ReferenceEquals(_forwardingExpiry, expiry)) return;

                _forwarding = null;
                _forwardingExpiry = null;
                ZLinkFrameworkDebugLog.SpotDiscovery($"mapping_evicted actor={ActorId}");
            }
        }
        catch (OperationCanceledException) when (expiry.IsCancellationRequested)
        {
        }
        finally
        {
            expiry.Dispose();
        }
    }

    public bool BindActorInstance(IZLinkActor actor)
    {
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
        IsConfigured = false;
        return true;
    }

    public ZLinkActorContext GetOrCreateContext(Func<ZLinkActorContext> createContext)
    {
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
        if (clearAssignedActor) Actor = null;
    }

    public void BindSession(
        RoutingId? sessionNodeRid,
        RoutingId sessionRid,
        string bindingToken)
    {
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

    public ZLinkActorBoundSession? ClearAfterDestroy()
    {
        ZLinkActorBoundSession? boundSession;
        lock (_sessionGate)
        {
            boundSession = _boundSession;
            _boundSession = null;
        }

        SessionId = null;
        Stream = null;
        NativeActorRef = null;
        Activation = null;
        CurrentDispatch = null;
        Context = null;
        Actor = null;
        ActorType = null;
        IsConfigured = false;
        IsDestroying = false;
        ContextInvalidated = true;
        _actorCreationTask = null;
        lock (_handoffGate)
        {
            _handoffCaptureActive = false;
            _handoffFrames.Clear();
            _forwarding = null;
            _handoffBarrier = null;
            _forwardingExpiry?.Cancel();
            _forwardingExpiry = null;
        }
        return boundSession;
    }

    public bool TryBeginDestroy()
    {
        if (IsDestroying) return false;

        IsDestroying = true;
        return true;
    }

    public void ResetDestroying()
    {
        IsDestroying = false;
    }

    public IZLinkSpot GetJoinedSpot()
    {
        EnsureContextValid();
        return LiveActivation?.Spot
               ?? throw new InvalidOperationException("Actor has not joined a SPOT.");
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

    public async ValueTask ClearFailedActorCreationAsync(Task<IZLinkActor> creationTask)
    {
        await ExecuteLockedAsync(
            () =>
            {
                if (!ReferenceEquals(_actorCreationTask, creationTask)) return;

                ClearFailedActorCreationLocked();
            },
            CancellationToken.None).ConfigureAwait(false);
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

    public async ValueTask ExecuteDispatchAsync(
        ZlinkStreamHeader header,
        Func<CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        using var turn = await _dispatchMailbox.EnterAsync(cancellationToken).ConfigureAwait(false);
        using var dispatch = EnterDispatch(header);
        await operation(cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask<T> ExecuteDispatchAsync<T>(
        ZlinkStreamHeader header,
        Func<CancellationToken, ValueTask<T>> operation,
        CancellationToken cancellationToken)
    {
        using var turn = await _dispatchMailbox.EnterAsync(cancellationToken).ConfigureAwait(false);
        using var dispatch = EnterDispatch(header);
        return await operation(cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask ExecuteLifecycleAsync(
        Func<CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        using var turn = await _dispatchMailbox.EnterAsync(cancellationToken).ConfigureAwait(false);
        await operation(cancellationToken).ConfigureAwait(false);
    }

    public DispatchScope EnterDispatch(ZlinkStreamHeader header)
    {
        var previous = CurrentDispatch;
        CurrentDispatch = new ZLinkActorDispatchState(header);
        return new DispatchScope(this, previous);
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
        if (!IsConfigured) Actor = null;

        if (Actor is null) ActorType = null;
    }

    public readonly struct DispatchScope : IDisposable
    {
        private readonly ZLinkActorRuntimeState? _state;
        private readonly ZLinkActorDispatchState? _previous;

        internal DispatchScope(
            ZLinkActorRuntimeState state,
            ZLinkActorDispatchState? previous)
        {
            _state = state;
            _previous = previous;
        }

        public void Dispose()
        {
            if (_state is not null) _state.CurrentDispatch = _previous;
        }
    }
}

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

internal readonly record struct ZLinkActorForwardingMapping(
    ZLinkBackendActorRef SourceActor,
    ZLinkBackendActorRef TargetActor,
    DateTimeOffset ExpiresAt);
