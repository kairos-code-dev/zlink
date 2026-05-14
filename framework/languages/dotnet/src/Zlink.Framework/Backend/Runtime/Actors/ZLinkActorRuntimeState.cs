namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorRuntimeState
{
    private readonly ZLinkActorPacketRegistry _packets = new();
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly ZLinkActorDispatchMailbox _dispatchMailbox = new();
    private Task<IZLinkActor>? _actorCreationTask;

    public string? SessionId { get; set; }

    public IZLinkStream? Stream { get; set; }

    public ZLinkBackendActorRef? NativeActorRef { get; set; }

    public ZLinkSpotActivation? Activation { get; set; }

    public ZLinkActorDispatchState? CurrentDispatch { get; private set; }

    public ZLinkActorContext? Context { get; set; }

    public IZLinkActor? Actor { get; set; }

    public bool IsConfigured { get; set; }

    public async ValueTask<ZLinkActorCreationOperation> GetOrStartActorCreationAsync(
        Func<Task<IZLinkActor>> createActor,
        CancellationToken cancellationToken)
    {
        var created = false;
        var task = await ExecuteLockedAsync(
            () =>
            {
                if (Actor is not null)
                {
                    return Task.FromResult(Actor);
                }

                if (_actorCreationTask is null)
                {
                    created = true;
                    _actorCreationTask = createActor();
                    _ = ClearActorCreationTaskWhenCompletedAsync(_actorCreationTask);
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

    public DispatchScope EnterDispatch(ZlinkStreamHeader header)
    {
        var previous = CurrentDispatch;
        CurrentDispatch = new ZLinkActorDispatchState(header);
        return new DispatchScope(this, previous);
    }

    public void AddPacket(IZLinkActor actor, Type handlerType, string? messageName)
    {
        _packets.Add(actor, handlerType, messageName);
    }

    public void ClearPacketRegistrations()
    {
        _packets.Clear();
    }

    public bool TryResolvePacket(
        ZlinkStreamHeader header,
        out ZLinkActorPacketDescriptor? descriptor)
    {
        return _packets.TryResolve(header, out descriptor);
    }

    public bool TryResolveRequest(
        string messageName,
        out ZLinkActorPacketDescriptor? descriptor)
    {
        return _packets.TryResolveRequest(messageName, out descriptor);
    }

    private async Task ClearActorCreationTaskWhenCompletedAsync(Task<IZLinkActor> creationTask)
    {
        try
        {
            await creationTask.ConfigureAwait(false);
        }
        catch
        {
        }

        await ExecuteLockedAsync(
            () =>
            {
                if (ReferenceEquals(_actorCreationTask, creationTask))
                {
                    _actorCreationTask = null;
                }
            },
            CancellationToken.None).ConfigureAwait(false);
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
            if (_state is not null)
            {
                _state.CurrentDispatch = _previous;
            }
        }
    }

}

internal readonly record struct ZLinkActorCreationOperation(
    Task<IZLinkActor> Task,
    bool Created);
