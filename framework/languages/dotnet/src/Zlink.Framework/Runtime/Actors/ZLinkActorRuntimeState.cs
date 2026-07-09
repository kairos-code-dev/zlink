namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorRuntimeState(string actorId)
{
    private readonly ZLinkActorDispatchMailbox _dispatchMailbox = new();
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly object _sessionGate = new();
    private Task<IZLinkActor>? _actorCreationTask;
    private ulong _actorGeneration;
    private ZLinkActorBoundSession? _boundSession;

    public string ActorId { get; } = actorId;

    public string? ActorType { get; private set; }

    public string? SessionId { get; set; }

    public IZLinkStream? Stream { get; set; }

    public ZLinkBackendActorRef? NativeActorRef { get; set; }

    public ZLinkSpotActivation? Activation { get; set; }

    public ZLinkActorDispatchState? CurrentDispatch { get; private set; }

    public ZLinkActorContext? Context { get; set; }

    public IZLinkActor? Actor { get; set; }

    public bool IsConfigured { get; set; }

    public bool ContextInvalidated { get; private set; }

    public bool IsDestroying { get; private set; }

    public ZLinkSpotActivation? LiveActivation
        => Activation is { IsDisposed: false } activation ? activation : null;

    public RoutingId? SpotRid => LiveActivation?.SpotRid;

    public bool IsJoined => LiveActivation is not null;

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

    public void EnsureActorGeneration(ulong nativeGeneration)
    {
        if (nativeGeneration != 0)
        {
            _actorGeneration = nativeGeneration;
            return;
        }

        if (_actorGeneration == 0) _actorGeneration = 1;
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
