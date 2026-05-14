using Zlink.Framework.Backend.Contracts;
using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorRuntimeState
{
    private readonly ZLinkActorPacketRegistry _packets = new();
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly SemaphoreSlim _dispatchGate = new(1, 1);

    public string? SessionId { get; set; }

    public IZLinkStream? Stream { get; set; }

    public ZLinkBackendActorRef? NativeActorRef { get; set; }

    public ZLinkSpotActivation? Activation { get; set; }

    public ZLinkActorDispatchState? CurrentDispatch { get; set; }

    public ZLinkActorContext? Context { get; set; }

    public IZLinkActor? Actor { get; set; }

    public bool IsConfigured { get; set; }

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
        await _dispatchGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        var previousDispatch = CurrentDispatch;
        CurrentDispatch = new ZLinkActorDispatchState(header);
        try
        {
            await operation(cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            CurrentDispatch = previousDispatch;
            _dispatchGate.Release();
        }
    }

    public async ValueTask<T> ExecuteDispatchAsync<T>(
        ZlinkStreamHeader header,
        Func<CancellationToken, ValueTask<T>> operation,
        CancellationToken cancellationToken)
    {
        await _dispatchGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        var previousDispatch = CurrentDispatch;
        CurrentDispatch = new ZLinkActorDispatchState(header);
        try
        {
            return await operation(cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            CurrentDispatch = previousDispatch;
            _dispatchGate.Release();
        }
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
        var arguments = descriptor.ActorType is null
            ? new[] { message, CreateSendContext(services, actor.ActorId, header, metadata, cancellationToken), cancellationToken }
            : [actor, message, cancellationToken];
        var result = descriptor.Invoker(handler, arguments);
        await ZLinkHandlerResultAwaiter.AwaitAsync(result).ConfigureAwait(false);
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
        var arguments = descriptor.ActorType is null
            ? new object[]
            {
                message!,
                new ZLinkActorRequestContext(
                    actor.ActorId,
                    string.Empty,
                    header.Name,
                    ZLinkEnvelopeCodec.DefaultContentType,
                    null,
                    null,
                    services,
                    cancellationToken,
                    metadata),
                cancellationToken
            }
            : [actor, message!, cancellationToken];
        var result = descriptor.Invoker(handler, arguments);
        var reply = await ZLinkHandlerResultAwaiter.AwaitAsync(result).ConfigureAwait(false);
        return ZLinkStreamPacketPayloadCodec.EncodeJson(reply, descriptor.ReplyType);
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
        var policy = services.GetRequiredService<IZLinkMessageMetadataPolicy>();
        var application = new Dictionary<string, string>(StringComparer.Ordinal);

        foreach (var (key, value) in header.Metadata.Values)
        {
            if (policy.CanForwardApplicationKey(key))
            {
                application[key] = value;
            }
        }

        if (application.Count == 0)
        {
            return ZLinkMessageMetadata.Empty;
        }

        return new ZLinkMessageMetadata(
            application,
            new Dictionary<string, string>(StringComparer.Ordinal));
    }

}
