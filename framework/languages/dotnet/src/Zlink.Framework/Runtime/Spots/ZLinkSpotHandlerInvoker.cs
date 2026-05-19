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
        if (!descriptor.ActorType.IsInstanceOfType(actor))
        {
            throw new InvalidOperationException(
                $"SPOT actor join handler '{descriptor.HandlerType}' expects actor '{descriptor.ActorType}', but received '{actor.GetType()}'.");
        }

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
        if (!descriptor.ActorType.IsInstanceOfType(actor))
        {
            throw new InvalidOperationException(
                $"SPOT actor packet handler '{descriptor.HandlerType}' expects actor '{descriptor.ActorType}', but received '{actor.GetType()}'.");
        }

        var message = ZLinkStreamPacketPayloadCodec.Decode(header, body, descriptor.MessageType);
        if (descriptor.Surface == ZLinkSpotActorHandlerSurface.EntrySpot
            && descriptor.SpotType is null)
        {
            await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, actor, message, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

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

        if (!descriptor.ActorType.IsInstanceOfType(actor))
        {
            throw new InvalidOperationException(
                $"SPOT actor packet handler '{descriptor.HandlerType}' expects actor '{descriptor.ActorType}', but received '{actor.GetType()}'.");
        }

        var message = ZLinkStreamPacketPayloadCodec.Decode(header, body, descriptor.MessageType);
        var reply = descriptor.Surface == ZLinkSpotActorHandlerSurface.EntrySpot
            && descriptor.SpotType is null
            ? await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, actor, message, cancellationToken)
                .ConfigureAwait(false)
            : await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, spot, actor, message, cancellationToken)
                .ConfigureAwait(false);
        return ZLinkStreamPacketPayloadCodec.EncodeJson(reply, descriptor.ReplyType);
    }

    public async ValueTask InvokeActorLifecycleAsync(
        ZLinkSpotActorLifecycleDescriptor descriptor,
        IZLinkActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        if (!descriptor.ActorType.IsInstanceOfType(actor))
        {
            throw new InvalidOperationException(
                $"SPOT actor lifecycle handler '{descriptor.HandlerType}' expects actor '{descriptor.ActorType}', but received '{actor.GetType()}'.");
        }

        if (descriptor.Surface == ZLinkSpotActorHandlerSurface.EntrySpot
            && descriptor.SpotType is null)
        {
            await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, actor, info, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, spot, actor, info, cancellationToken)
            .ConfigureAwait(false);
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
