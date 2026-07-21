using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRouteHandlerInvoker(
    IServiceProvider services,
    ZLinkCodecRegistryBuilder codecs)
{
    public async ValueTask InvokeSendAsync(
        ZLinkRouteHandlerDescriptor descriptor,
        string routerChannelId,
        RoutingId sourceRid,
        ZLinkEnvelopeHeader header,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken,
        ZLinkMessageMetadata? metadata = null)
    {
        var message = ZLinkEnvelopeCodec.DecodeBody(parts, descriptor.MessageType, codecs);

        await using var scope = services.CreateAsyncScope();
        var context = new ZLinkRouteSendContext(
            routerChannelId,
            null,
            sourceRid,
            header.MessageName!,
            header.ContentType,
            cancellationToken,
            metadata);
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
        CancellationToken cancellationToken,
        ZLinkMessageMetadata? metadata = null)
    {
        var message = ZLinkEnvelopeCodec.DecodeBody(parts, descriptor.MessageType, codecs);

        await using var scope = services.CreateAsyncScope();
        var context = new ZLinkRouteRequestContext(
            routerChannelId,
            null,
            sourceRid,
            header.MessageName!,
            header.ContentType,
            cancellationToken,
            metadata);
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
