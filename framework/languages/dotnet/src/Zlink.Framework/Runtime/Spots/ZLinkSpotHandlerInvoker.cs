using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Streams;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotHandlerInvoker(
    IServiceProvider services,
    object spot,
    ZLinkCodecRegistryBuilder codecs)
{
    public async ValueTask InvokePacketAsync(
        ZLinkSpotDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, spot, message, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<object?> InvokeRequestAsync(
        ZLinkSpotDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        return await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, spot, message, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask InvokeSubscriptionAsync(
        ZLinkSpotSubscriptionDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, spot, message, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask InvokeTimerAsync(
        ZLinkSpotTimerDescriptor descriptor,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, spot, tick, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ZLinkSpotActorJoinResult> InvokeActorJoinAsync(
        ZLinkSpotActorJoinDescriptor descriptor,
        IZLinkActor actor,
        Message request,
        CancellationToken cancellationToken)
    {
        EnsureActorType(
            descriptor.HandlerType,
            descriptor.ActorType,
            actor,
            "SPOT actor join callback");

        if (descriptor.PassSpotArgument)
        {
            var result = await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, spot, actor, request, cancellationToken)
                .ConfigureAwait(false);
            return (ZLinkSpotActorJoinResult)result!;
        }

        var hookResult = await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, actor, request, cancellationToken)
            .ConfigureAwait(false);
        return (ZLinkSpotActorJoinResult)hookResult!;
    }

    public async ValueTask InvokeActorPacketAsync(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        EnsureActorType(
            descriptor.HandlerType,
            descriptor.ActorType,
            actor,
            "SPOT actor packet handler");

        var message = ZLinkStreamPacketPayloadCodec.Decode(header, body, descriptor.MessageType, codecs);
        var context = CreateSendContext(header, cancellationToken);
        await InvokeAsync(
                descriptor.HandlerType,
                descriptor.Invoker,
                spot,
                actor,
                context,
                message,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<ZLinkActorReply> InvokeActorPacketForReplyAsync(
        ZLinkSpotActorPacketDescriptor descriptor,
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message body,
        CancellationToken cancellationToken)
    {
        if (descriptor.ReplyType is null)
        {
            throw new InvalidOperationException(
                $"Actor packet handler '{descriptor.HandlerType}' does not declare a reply type.");
        }

        EnsureActorType(
            descriptor.HandlerType,
            descriptor.ActorType,
            actor,
            "SPOT actor packet handler");

        var message = ZLinkStreamPacketPayloadCodec.Decode(header, body, descriptor.MessageType, codecs);
        var context = CreateRequestContext(header, cancellationToken);
        var reply = await InvokeAsync(
                descriptor.HandlerType,
                descriptor.Invoker,
                spot,
                actor,
                context,
                message,
                cancellationToken)
            .ConfigureAwait(false);
        var encoded = ZLinkStreamPacketPayloadCodec.Encode(reply, descriptor.ReplyType, codecs);
        return ZLinkActorReply.FromPayload(
            encoded.Codec,
            encoded.Payload.ToArray(),
            context.Reply.CreateSnapshot());
    }

    private ZLinkSpotActorSendContext CreateSendContext(
        ZlinkStreamHeader header,
        CancellationToken cancellationToken)
    {
        return new ZLinkSpotActorSendContext(
            header.Name,
            ZLinkEnvelopeCodec.DefaultContentType,
            cancellationToken,
            CreateMessageMetadata(header));
    }

    private ZLinkSpotActorRequestContext CreateRequestContext(
        ZlinkStreamHeader header,
        CancellationToken cancellationToken)
    {
        return new ZLinkSpotActorRequestContext(
            header.Name,
            ZLinkEnvelopeCodec.DefaultContentType,
            cancellationToken,
            CreateMessageMetadata(header));
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
            if (policy.CanForward(key))
            {
                application ??= new Dictionary<string, string>(StringComparer.Ordinal);
                application[key] = value;
            }
        }

        if (application is null)
        {
            return ZLinkMessageMetadata.Empty;
        }

        return new ZLinkMessageMetadata(application);
    }

    public async ValueTask InvokeActorLifecycleAsync(
        ZLinkSpotActorLifecycleDescriptor descriptor,
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        EnsureActorType(
            descriptor.HandlerType,
            descriptor.ActorType,
            actor,
            "SPOT actor lifecycle handler");

        if (descriptor.PassSpotArgument)
        {
            await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, spot, actor, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, actor, cancellationToken)
            .ConfigureAwait(false);
    }

    private static void EnsureActorType(
        Type handlerType,
        Type expectedActorType,
        IZLinkActor actor,
        string handlerKind)
    {
        if (expectedActorType.IsInstanceOfType(actor))
        {
            return;
        }

        throw new InvalidOperationException(
            $"{handlerKind} '{handlerType}' expects actor '{expectedActorType}', but received '{actor.GetType()}'.");
    }

    private ValueTask<object?> InvokeAsync(
        Type handlerType,
        ZLinkHandlerMethodInvoker invoker,
        object? arg0,
        object? arg1)
    {
        var handler = ResolveHandler(handlerType);
        return ZLinkHandlerInvocationEngine.InvokeAsync(
            handler,
            invoker,
            2,
            arguments =>
            {
                arguments[0] = arg0;
                arguments[1] = arg1;
            });
    }

    private ValueTask<object?> InvokeAsync(
        Type handlerType,
        ZLinkHandlerMethodInvoker invoker,
        object? arg0,
        object? arg1,
        object? arg2)
    {
        var handler = ResolveHandler(handlerType);
        return ZLinkHandlerInvocationEngine.InvokeAsync(
            handler,
            invoker,
            3,
            arguments =>
            {
                arguments[0] = arg0;
                arguments[1] = arg1;
                arguments[2] = arg2;
            });
    }

    private ValueTask<object?> InvokeAsync(
        Type handlerType,
        ZLinkHandlerMethodInvoker invoker,
        object? arg0,
        object? arg1,
        object? arg2,
        object? arg3)
    {
        var handler = ResolveHandler(handlerType);
        return ZLinkHandlerInvocationEngine.InvokeAsync(
            handler,
            invoker,
            4,
            arguments =>
            {
                arguments[0] = arg0;
                arguments[1] = arg1;
                arguments[2] = arg2;
                arguments[3] = arg3;
            });
    }

    private ValueTask<object?> InvokeAsync(
        Type handlerType,
        ZLinkHandlerMethodInvoker invoker,
        object? arg0,
        object? arg1,
        object? arg2,
        object? arg3,
        object? arg4)
    {
        var handler = ResolveHandler(handlerType);
        return ZLinkHandlerInvocationEngine.InvokeAsync(
            handler,
            invoker,
            5,
            arguments =>
            {
                arguments[0] = arg0;
                arguments[1] = arg1;
                arguments[2] = arg2;
                arguments[3] = arg3;
                arguments[4] = arg4;
            });
    }

    private object ResolveHandler(Type handlerType)
    {
        return handlerType.IsInstanceOfType(spot)
            ? spot
            : ActivatorUtilities.GetServiceOrCreateInstance(services, handlerType);
    }
}
