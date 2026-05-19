using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRouteHandlerInvoker(IServiceProvider services)
{
    public async ValueTask InvokeSendAsync(
        ZLinkRouteHandlerDescriptor descriptor,
        string routerChannelId,
        RoutingId sourceRid,
        ZLinkEnvelopeHeader header,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        var message = ZLinkEnvelopeCodec.DecodeBody(parts, descriptor.MessageType);

        await using var scope = services.CreateAsyncScope();
        var context = new ZLinkRouteSendContext(
            routerChannelId,
            sourceRid,
            header.MessageName,
            header.ContentType,
            header.CorrelationId,
            scope.ServiceProvider,
            cancellationToken);
        var handler = scope.ServiceProvider.GetRequiredService(descriptor.HandlerType);
        await ZLinkHandlerInvocationEngine.InvokeAsync(
                handler,
                descriptor.Invoker,
                3,
                arguments =>
                {
                    arguments[0] = message;
                    arguments[1] = context;
                    arguments[2] = cancellationToken;
                })
            .ConfigureAwait(false);
    }

    public async ValueTask<ZLinkRouteHandlerReply> InvokeRequestAsync(
        ZLinkRouteHandlerDescriptor descriptor,
        string routerChannelId,
        RoutingId sourceRid,
        ZLinkEnvelopeHeader header,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        var message = ZLinkEnvelopeCodec.DecodeBody(parts, descriptor.MessageType);

        await using var scope = services.CreateAsyncScope();
        var context = new ZLinkRouteRequestContext(
            routerChannelId,
            sourceRid,
            header.MessageName,
            header.ContentType,
            header.CorrelationId,
            header.Deadline,
            scope.ServiceProvider,
            cancellationToken);
        var handler = scope.ServiceProvider.GetRequiredService(descriptor.HandlerType);
        var reply = await ZLinkHandlerInvocationEngine.InvokeAsync(
                handler,
                descriptor.Invoker,
                3,
                arguments =>
                {
                    arguments[0] = message;
                    arguments[1] = context;
                    arguments[2] = cancellationToken;
                })
            .ConfigureAwait(false);
        return new ZLinkRouteHandlerReply(reply, descriptor.ReplyType);
    }
}

internal readonly record struct ZLinkRouteHandlerReply(
    object? Message,
    Type? MessageType);
