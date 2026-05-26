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
        await ZLinkHandlerInvocationEngine.InvokeAsync(
                handler,
                descriptor.Invoker,
                3,
                arguments =>
                {
                    arguments[0] = actor;
                    arguments[1] = message;
                    arguments[2] = cancellationToken;
                })
            .ConfigureAwait(false);
    }

    public async ValueTask<ZLinkActorReply> DispatchForReplyAsync(
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
        var reply = await ZLinkHandlerInvocationEngine.InvokeAsync(
                handler,
                descriptor.Invoker,
                3,
                arguments =>
                {
                    arguments[0] = actor;
                    arguments[1] = message!;
                    arguments[2] = cancellationToken;
                })
            .ConfigureAwait(false);
        return ZLinkActorReply.FromPayload(
            ZLinkStreamPacketPayloadCodec.EncodeJson(reply, descriptor.ReplyType));
    }

    private static void ValidateActorType(
        ZLinkActorPacketDescriptor descriptor,
        IZLinkActor actor)
    {
        if (!descriptor.ActorType.IsInstanceOfType(actor))
        {
            throw new InvalidOperationException(
                $"Actor packet handler '{descriptor.HandlerType}' expects actor '{descriptor.ActorType}', but received '{actor.GetType()}'.");
        }
    }
}
