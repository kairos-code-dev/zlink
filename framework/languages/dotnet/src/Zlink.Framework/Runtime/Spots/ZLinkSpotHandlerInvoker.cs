using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotHandlerInvoker(IServiceProvider services, IZLinkSpot spot)
{
    public async ValueTask InvokePacketAsync(
        ZLinkSpotDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        await InvokeAsync(descriptor.HandlerType, descriptor.HandleMethod, [spot, message, cancellationToken])
            .ConfigureAwait(false);
    }

    public async ValueTask<object?> InvokeRequestAsync(
        ZLinkSpotDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        return await InvokeAsync(descriptor.HandlerType, descriptor.HandleMethod, [spot, message, cancellationToken])
            .ConfigureAwait(false);
    }

    public async ValueTask InvokeSubscriptionAsync(
        ZLinkSpotSubscriptionDescriptor descriptor,
        object? message,
        CancellationToken cancellationToken)
    {
        await InvokeAsync(descriptor.HandlerType, descriptor.HandleMethod, [spot, message, cancellationToken])
            .ConfigureAwait(false);
    }

    public async ValueTask InvokeTimerAsync(
        ZLinkSpotTimerDescriptor descriptor,
        CancellationToken cancellationToken)
    {
        await InvokeAsync(descriptor.HandlerType, descriptor.HandleMethod, [spot, cancellationToken])
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

        return await InvokeAsync(descriptor.HandlerType, descriptor.HandleMethod, [spot, actor, request, cancellationToken])
            .ConfigureAwait(false);
    }

    private async ValueTask<object?> InvokeAsync(
        Type handlerType,
        System.Reflection.MethodInfo method,
        object?[] arguments)
    {
        var handler = services.GetRequiredService(handlerType);
        var result = method.Invoke(handler, arguments);
        return await ZLinkHandlerResultAwaiter.AwaitAsync(result).ConfigureAwait(false);
    }
}
