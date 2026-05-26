namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotHandlerInvoker(IServiceProvider services, object spot)
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

    public async ValueTask<object?> InvokeActorJoinAsync(
        ZLinkSpotActorJoinDescriptor descriptor,
        IZLinkActor actor,
        object request,
        CancellationToken cancellationToken)
    {
        EnsureActorType(
            descriptor.HandlerType,
            descriptor.ActorType,
            actor,
            "SPOT actor join handler");

        return await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, spot, actor, request, cancellationToken)
            .ConfigureAwait(false);
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

        var message = ZLinkStreamPacketPayloadCodec.Decode(header, body, descriptor.MessageType);
        await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, spot, actor, message, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask<byte[]> InvokeActorPacketForReplyAsync(
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

        var message = ZLinkStreamPacketPayloadCodec.Decode(header, body, descriptor.MessageType);
        var reply = await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, spot, actor, message, cancellationToken)
            .ConfigureAwait(false);
        return ZLinkStreamPacketPayloadCodec.EncodeJson(reply, descriptor.ReplyType);
    }

    public async ValueTask InvokeActorLifecycleAsync(
        ZLinkSpotActorLifecycleDescriptor descriptor,
        IZLinkActor actor,
        ZLinkSpotActorChangeResult context,
        CancellationToken cancellationToken)
    {
        EnsureActorType(
            descriptor.HandlerType,
            descriptor.ActorType,
            actor,
            "SPOT actor lifecycle handler");

        await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, spot, actor, context, cancellationToken)
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
        return ZLinkHandlerInvocationEngine.InvokeAsync(
            services,
            handlerType,
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
        return ZLinkHandlerInvocationEngine.InvokeAsync(
            services,
            handlerType,
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
        return ZLinkHandlerInvocationEngine.InvokeAsync(
            services,
            handlerType,
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

}
