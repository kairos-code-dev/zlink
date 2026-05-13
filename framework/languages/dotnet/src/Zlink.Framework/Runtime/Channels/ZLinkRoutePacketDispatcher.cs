using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRoutePacketDispatcher(
    string routerChannelId,
    IZLinkBackendRouterSocket router,
    ZLinkRouteHandlerRegistry handlers,
    ZLinkRouteHandlerInvoker handlerInvoker,
    IZLinkRouteInternalPacketDispatcher internalPackets)
{
    public async ValueTask DispatchAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        if (received.Parts.Count == 0)
        {
            return;
        }

        var header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts[0]);
        if (header.Kind == ZLinkMessageKind.Command)
        {
            await DispatchSendAsync(received, header, cancellationToken).ConfigureAwait(false);
            return;
        }

        if (header.Kind == ZLinkMessageKind.Request)
        {
            await DispatchRequestAsync(received, header, cancellationToken).ConfigureAwait(false);
        }
    }

    private async ValueTask DispatchSendAsync(
        Received received,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        if (internalPackets.CanHandleSend(header.MessageName))
        {
            await internalPackets.DispatchSendAsync(received, cancellationToken).ConfigureAwait(false);
            return;
        }

        var descriptor = handlers.Get(routerChannelId, ZLinkMessageKind.Command, header.MessageName);
        var sourceRid = received.RoutingId
            ?? throw new InvalidOperationException("Route send requires a source routing id.");

        await handlerInvoker.InvokeSendAsync(
                descriptor,
                routerChannelId,
                sourceRid,
                header,
                received.Parts[0],
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask DispatchRequestAsync(
        Received received,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken)
    {
        if (internalPackets.CanHandleRequest(header.MessageName))
        {
            await DispatchInternalRequestAsync(
                received,
                header,
                cancellationToken,
                (_, requestHeader, token) => internalPackets.DispatchRequestAsync(received, requestHeader, token))
                .ConfigureAwait(false);
            return;
        }

        var descriptor = handlers.Get(routerChannelId, ZLinkMessageKind.Request, header.MessageName);
        var sourceRid = received.RoutingId
            ?? throw new InvalidOperationException("Route request requires a source routing id.");

        try
        {
            var reply = await handlerInvoker.InvokeRequestAsync(
                    descriptor,
                    routerChannelId,
                    sourceRid,
                    header,
                    received.Parts[0],
                    cancellationToken)
                .ConfigureAwait(false);
            Reply(sourceRid, received.RequestSeq, header, reply.Message, reply.MessageType);
        }
        catch (Exception ex)
        {
            ReplyError(sourceRid, received.RequestSeq, header, ex);
        }
    }

    private async ValueTask DispatchInternalRequestAsync(
        Received received,
        ZLinkEnvelopeHeader header,
        CancellationToken cancellationToken,
        Func<RoutingId, ZLinkEnvelopeHeader, CancellationToken, ValueTask<byte[]>> dispatch)
    {
        var sourceRid = received.RoutingId
            ?? throw new InvalidOperationException("Internal routed request requires a source routing id.");

        try
        {
            var reply = await dispatch(sourceRid, header, cancellationToken).ConfigureAwait(false);
            Reply(sourceRid, received.RequestSeq, header, reply, typeof(byte[]));
        }
        catch (Exception ex)
        {
            ReplyError(sourceRid, received.RequestSeq, header, ex);
        }
    }

    private void Reply(
        RoutingId sourceRid,
        ulong? requestSeq,
        ZLinkEnvelopeHeader requestHeader,
        object? reply,
        Type? replyType)
    {
        var replyHeader = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Response,
            routerChannelId,
            requestHeader.MessageName,
            ZLinkEnvelopeCodec.DefaultContentType,
            requestHeader.CorrelationId,
            null,
            null,
            null,
            null);
        using var replyMessage = ZLinkEnvelopeCodec.Encode(replyHeader, reply, replyType);
        router.Reply(sourceRid, requestSeq ?? 0UL, replyMessage);
    }

    private void ReplyError(
        RoutingId sourceRid,
        ulong? requestSeq,
        ZLinkEnvelopeHeader requestHeader,
        Exception exception)
    {
        var errorHeader = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Error,
            routerChannelId,
            requestHeader.MessageName,
            ZLinkEnvelopeCodec.DefaultContentType,
            requestHeader.CorrelationId,
            null,
            null,
            exception.GetType().Name,
            exception.Message);
        using var replyMessage = ZLinkEnvelopeCodec.Encode(errorHeader, null, null);
        router.Reply(sourceRid, requestSeq ?? 0UL, replyMessage);
    }
}
