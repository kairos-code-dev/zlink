using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Streams;

namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorPacketDispatcher(IServiceProvider services)
{
    private readonly ILogger<ZLinkActorPacketDispatcher> _logger =
        services.GetService<ILoggerFactory>()?.CreateLogger<ZLinkActorPacketDispatcher>()
        ?? NullLogger<ZLinkActorPacketDispatcher>.Instance;

    public async ValueTask DispatchAsync(
        ZLinkActorRuntimeState state,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        if (!state.TryResolvePacket(header, out var descriptor) || descriptor is null)
        {
            ZLinkMessageFlowLogger.Dropped(
                _logger,
                LogLevel.Warning,
                "Actor",
                "Send",
                header.Name,
                "no-handler",
                actorId: actor.ActorId,
                actorType: actor.GetType().Name);
            return;
        }

        ValidateActorType(descriptor, actor);

        var message = ZLinkStreamPacketPayloadCodec.Decode(header, payload, descriptor.MessageType);
        var handler = ActivatorUtilities.GetServiceOrCreateInstance(
            services,
            descriptor.HandlerType);
        var metadata = CreateMessageMetadata(header);
        await ZLinkHandlerInvocationEngine.InvokeAsync(
                handler,
                descriptor.Invoker,
                3,
                arguments =>
                {
                    arguments[0] = descriptor.ActorType is null ? message : actor;
                    arguments[1] = descriptor.ActorType is null
                        ? CreateSendContext(actor.ActorId, header, metadata, cancellationToken)
                        : message;
                    arguments[2] = cancellationToken;
                })
            .ConfigureAwait(false);
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
            ZLinkMessageFlowLogger.HandlerMissing(
                _logger,
                LogLevel.Warning,
                "Actor",
                "Request",
                header.Name,
                "reply-error",
                "no-handler",
                actorId: actor.ActorId,
                actorType: actor.GetType().Name);
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorDispatchHandlerNotFound,
                $"No actor request handler is registered for '{header.Name}'.");
        }

        ValidateActorType(descriptor, actor);

        var message = ZLinkStreamPacketPayloadCodec.Decode(header, payload, descriptor.MessageType);
        var handler = ActivatorUtilities.GetServiceOrCreateInstance(
            services,
            descriptor.HandlerType);
        var metadata = CreateMessageMetadata(header);
        var reply = await ZLinkHandlerInvocationEngine.InvokeAsync(
                handler,
                descriptor.Invoker,
                3,
                arguments =>
                {
                    arguments[0] = descriptor.ActorType is null ? message! : actor;
                    arguments[1] = descriptor.ActorType is null
                        ? new ZLinkActorRequestContext(
                            actor.ActorId,
                            header.Name,
                            ZLinkEnvelopeCodec.DefaultContentType,
                            null,
                            null,
                            CreateBoundSession(actor.ActorId),
                            services,
                            cancellationToken,
                            metadata)
                        : message!;
                    arguments[2] = cancellationToken;
                })
            .ConfigureAwait(false);
        return ZLinkStreamPacketPayloadCodec.EncodeJson(reply, descriptor.ReplyType);
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
            header.Name,
            ZLinkEnvelopeCodec.DefaultContentType,
            null,
            CreateBoundSession(actorId),
            services,
            cancellationToken,
            metadata);
    }

    private IZLinkBoundSession CreateBoundSession(string actorId)
    {
        return services.GetRequiredService<IZLinkBoundSessionFactory>()
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
