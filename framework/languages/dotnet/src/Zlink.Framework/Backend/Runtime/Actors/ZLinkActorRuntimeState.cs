using System.Buffers;
using Zlink.Framework.Backend.Contracts;
using Microsoft.Extensions.DependencyInjection;

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

    public async ValueTask DispatchAsync(
        IServiceProvider services,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        if (!_packets.TryResolve(header, out var descriptor) || descriptor is null)
        {
            return;
        }

        if (descriptor.ActorType is not null && !descriptor.ActorType.IsInstanceOfType(actor))
        {
            throw new InvalidOperationException(
                $"Actor packet handler '{descriptor.HandlerType}' expects actor '{descriptor.ActorType}', but received '{actor.GetType()}'.");
        }

        var message = ZLinkStreamPacketPayloadCodec.Decode(header, body, descriptor.MessageType);
        var handler = services.GetRequiredService(descriptor.HandlerType);
        var metadata = CreateMessageMetadata(services, header);
        var arguments = ArrayPool<object?>.Shared.Rent(3);
        arguments[0] = descriptor.ActorType is null ? message : actor;
        arguments[1] = descriptor.ActorType is null
            ? CreateSendContext(services, actor.ActorId, header, metadata, cancellationToken)
            : message;
        arguments[2] = cancellationToken;
        try
        {
            var result = descriptor.Invoker(handler, arguments);
            await ZLinkHandlerResultAwaiter.AwaitAsync(result).ConfigureAwait(false);
        }
        finally
        {
            Array.Clear(arguments, 0, 3);
            ArrayPool<object?>.Shared.Return(arguments);
        }
    }

    public async ValueTask<byte[]> DispatchForReplyAsync(
        IServiceProvider services,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        if (!_packets.TryResolveRequest(header.Name, out var descriptor)
            || descriptor is null
            || descriptor.ReplyType is null)
        {
            throw new InvalidOperationException($"No actor request handler is registered for '{header.Name}'.");
        }

        var message = ZLinkStreamPacketPayloadCodec.Decode(header, body, descriptor.MessageType);
        var handler = services.GetRequiredService(descriptor.HandlerType);
        var metadata = CreateMessageMetadata(services, header);
        var arguments = ArrayPool<object?>.Shared.Rent(3);
        arguments[0] = descriptor.ActorType is null ? message! : actor;
        arguments[1] = descriptor.ActorType is null
            ? new ZLinkActorRequestContext(
                actor.ActorId,
                string.Empty,
                header.Name,
                ZLinkEnvelopeCodec.DefaultContentType,
                null,
                null,
                services,
                cancellationToken,
                metadata)
            : message!;
        arguments[2] = cancellationToken;
        try
        {
            var result = descriptor.Invoker(handler, arguments);
            var reply = await ZLinkHandlerResultAwaiter.AwaitAsync(result).ConfigureAwait(false);
            return ZLinkStreamPacketPayloadCodec.EncodeJson(reply, descriptor.ReplyType);
        }
        finally
        {
            Array.Clear(arguments, 0, 3);
            ArrayPool<object?>.Shared.Return(arguments);
        }
    }

    private static ZLinkActorSendContext CreateSendContext(
        IServiceProvider services,
        string actorId,
        ZlinkStreamHeader header,
        ZLinkMessageMetadata metadata,
        CancellationToken cancellationToken)
    {
        return new ZLinkActorSendContext(
            actorId,
            string.Empty,
            header.Name,
            ZLinkEnvelopeCodec.DefaultContentType,
            null,
            services,
            cancellationToken,
            metadata);
    }

    private static ZLinkMessageMetadata CreateMessageMetadata(
        IServiceProvider services,
        ZlinkStreamHeader header)
    {
        if (header.Metadata.Count == 0)
        {
            return ZLinkMessageMetadata.Empty;
        }

        var policy = services.GetRequiredService<IZLinkMessageMetadataPolicy>();
        Dictionary<string, string>? application = null;

        foreach (var (key, value) in header.Metadata.Values)
        {
            if (policy.CanForwardApplicationKey(key))
            {
                application ??= new Dictionary<string, string>(StringComparer.Ordinal);
                application[key] = value;
            }
        }

        if (application is null)
        {
            return ZLinkMessageMetadata.Empty;
        }

        return new ZLinkMessageMetadata(
            application,
            new Dictionary<string, string>(StringComparer.Ordinal));
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
