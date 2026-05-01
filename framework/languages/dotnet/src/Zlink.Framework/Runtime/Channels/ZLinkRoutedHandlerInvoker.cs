using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRoutedHandlerInvoker(IServiceProvider services)
{
    public async ValueTask InvokeSendAsync(
        ZLinkRoutedHandlerDescriptor descriptor,
        string routerChannelId,
        RoutingId sourceRid,
        ZLinkEnvelopeHeader header,
        Message envelope,
        CancellationToken cancellationToken)
    {
        var message = ZLinkEnvelopeCodec.DecodeBody(envelope, descriptor.MessageType);

        await using var scope = services.CreateAsyncScope();
        var context = new ZLinkRoutedSendContext(
            routerChannelId,
            sourceRid,
            header.MessageName,
            header.ContentType,
            header.CorrelationId,
            scope.ServiceProvider,
            cancellationToken);
        var handler = scope.ServiceProvider.GetRequiredService(descriptor.HandlerType);
        var result = descriptor.HandleMethod.Invoke(handler, [message, context, cancellationToken]);
        await ZLinkHandlerResultAwaiter.AwaitAsync(result).ConfigureAwait(false);
    }

    public async ValueTask<ZLinkRoutedHandlerReply> InvokeRequestAsync(
        ZLinkRoutedHandlerDescriptor descriptor,
        string routerChannelId,
        RoutingId sourceRid,
        ZLinkEnvelopeHeader header,
        Message envelope,
        CancellationToken cancellationToken)
    {
        var message = ZLinkEnvelopeCodec.DecodeBody(envelope, descriptor.MessageType);

        await using var scope = services.CreateAsyncScope();
        var context = new ZLinkRoutedRequestContext(
            routerChannelId,
            sourceRid,
            header.MessageName,
            header.ContentType,
            header.CorrelationId,
            header.Deadline,
            scope.ServiceProvider,
            cancellationToken);
        var handler = scope.ServiceProvider.GetRequiredService(descriptor.HandlerType);
        var result = descriptor.HandleMethod.Invoke(handler, [message, context, cancellationToken]);
        var reply = await ZLinkHandlerResultAwaiter.AwaitAsync(result).ConfigureAwait(false);
        return new ZLinkRoutedHandlerReply(reply, descriptor.ReplyType);
    }
}

internal readonly record struct ZLinkRoutedHandlerReply(
    object? Message,
    Type? MessageType);
