using System.Buffers;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Streams;

namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorPacketDispatcher(IServiceProvider services)
{
    public async ValueTask DispatchAsync(
        ZLinkActorRuntimeState state,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        if (!state.TryResolvePacket(header, out var descriptor) || descriptor is null)
        {
            return;
        }

        ValidateActorType(descriptor, actor);

        var message = ZLinkStreamPacketPayloadCodec.Decode(header, payload, descriptor.MessageType);
        var handler = services.GetRequiredService(descriptor.HandlerType);
        var metadata = CreateMessageMetadata(header);
        var arguments = ArrayPool<object?>.Shared.Rent(3);
        arguments[0] = descriptor.ActorType is null ? message : actor;
        arguments[1] = descriptor.ActorType is null
            ? CreateSendContext(actor.ActorId, header, metadata, cancellationToken)
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
        ZLinkActorRuntimeState state,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        if (!state.TryResolveRequest(header.Name, out var descriptor)
            || descriptor is null
            || descriptor.ReplyType is null)
        {
            throw new InvalidOperationException($"No actor request handler is registered for '{header.Name}'.");
        }

        ValidateActorType(descriptor, actor);

        var message = ZLinkStreamPacketPayloadCodec.Decode(header, payload, descriptor.MessageType);
        var handler = services.GetRequiredService(descriptor.HandlerType);
        var metadata = CreateMessageMetadata(header);
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
                CreateSessionProxy(actor.ActorId),
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

    private static void ValidateActorType(
        ZLinkActorPacketDescriptor descriptor,
        IZLinkActor actor)
    {
        if (descriptor.ActorType is not null && !descriptor.ActorType.IsInstanceOfType(actor))
        {
            throw new InvalidOperationException(
                $"Actor packet handler '{descriptor.HandlerType}' expects actor '{descriptor.ActorType}', but received '{actor.GetType()}'.");
        }
    }

    private ZLinkActorSendContext CreateSendContext(
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
            CreateSessionProxy(actorId),
            services,
            cancellationToken,
            metadata);
    }

    private IZLinkSessionProxy CreateSessionProxy(string actorId)
    {
        return services.GetRequiredService<IZLinkSessionProxyFactory>()
            .Create(actorId);
    }

    private ZLinkMessageMetadata CreateMessageMetadata(ZlinkStreamHeader header)
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
}
