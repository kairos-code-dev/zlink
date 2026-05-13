using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotHandlerInvoker(IServiceProvider services, object spot)
{
    public async ValueTask InvokePacketAsync(
        ZLinkSpotDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, [spot, message, cancellationToken])
            .ConfigureAwait(false);
    }

    public async ValueTask<object?> InvokeRequestAsync(
        ZLinkSpotDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        return await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, [spot, message, cancellationToken])
            .ConfigureAwait(false);
    }

    public async ValueTask InvokeSubscriptionAsync(
        ZLinkSpotSubscriptionDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, [spot, message, cancellationToken])
            .ConfigureAwait(false);
    }

    public async ValueTask InvokeTimerAsync(
        ZLinkSpotTimerDescriptor descriptor,
        CancellationToken cancellationToken)
    {
        await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, [spot, cancellationToken])
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

        return await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, [spot, actor, request, cancellationToken])
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
        object?[] arguments = descriptor.Surface == ZLinkSpotActorHandlerSurface.EntrySpot
            ? [actor, message, cancellationToken]
            : [spot, actor, message, cancellationToken];

        await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, arguments)
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
        object?[] arguments = descriptor.Surface == ZLinkSpotActorHandlerSurface.EntrySpot
            ? [actor, message, cancellationToken]
            : [spot, actor, message, cancellationToken];

        var reply = await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, arguments)
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

        object?[] arguments = descriptor.Surface == ZLinkSpotActorHandlerSurface.EntrySpot
            ? [actor, info, cancellationToken]
            : [spot, actor, info, cancellationToken];

        await InvokeAsync(descriptor.HandlerType, descriptor.Invoker, arguments)
            .ConfigureAwait(false);
    }

    private async ValueTask<object?> InvokeAsync(
        Type handlerType,
        ZLinkHandlerMethodInvoker invoker,
        object?[] arguments)
    {
        var handler = services.GetRequiredService(handlerType);
        var result = invoker(handler, arguments);
        return await ZLinkHandlerResultAwaiter.AwaitAsync(result).ConfigureAwait(false);
    }

}
