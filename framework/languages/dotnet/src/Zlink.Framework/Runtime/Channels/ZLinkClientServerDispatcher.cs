namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkClientServerDispatcher(
    ZLinkChannelCommandDispatchPipeline commandPipeline,
    ZLinkChannelRequestDispatchPipeline requestPipeline,
    ZLinkCodecRegistryBuilder codecs)
{
    public async ValueTask DispatchAsync(
        string channelName,
        IZLinkBackendRouterSocket router,
        Received received,
        CancellationToken cancellationToken)
    {
        ZLinkEnvelopeHeader header;
        try
        {
            header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts);
            if (!StringComparer.Ordinal.Equals(header.ChannelName, channelName))
                throw new ZLinkEnvelopeProtocolException(
                    header,
                    $"ClientServer channel '{channelName}' received an envelope for "
                    + $"'{header.ChannelName}'.");
        }
        catch (ZLinkEnvelopeProtocolException protocolError)
        {
            ReplyProtocolError(
                channelName,
                router,
                received,
                protocolError.Header,
                protocolError.Message);
            return;
        }

        switch (header.Kind)
        {
            case ZLinkMessageKind.Command:
                await commandPipeline.DispatchAsync(
                        channelName,
                        received.Parts,
                        header,
                        cancellationToken)
                    .ConfigureAwait(false);
                break;
            case ZLinkMessageKind.Request:
                await requestPipeline.DispatchAsync(
                        channelName,
                        received.Parts,
                        header,
                        (replyHeader, reply, replyType) =>
                            Reply(router, received, replyHeader, reply, replyType),
                        errorHeader => Reply(router, received, errorHeader, null, null),
                        cancellationToken)
                    .ConfigureAwait(false);
                break;
            default:
                ReplyProtocolError(
                    channelName,
                    router,
                    received,
                    header,
                    $"ClientServer server cannot accept '{header.Kind}' envelopes.");
                break;
        }
    }

    private void ReplyProtocolError(
        string channelName,
        IZLinkBackendRouterSocket router,
        Received received,
        ZLinkEnvelopeHeader request,
        string message)
    {
        if (!ZLinkEnvelopeCodec.CanCorrelateReply(request))
            return;
        Reply(
            router,
            received,
            ZLinkChannelReplyWriter.CreateProtocolErrorHeader(
                channelName,
                request,
                message),
            null,
            null);
    }

    private void Reply(
        IZLinkBackendRouterSocket router,
        Received received,
        ZLinkEnvelopeHeader header,
        object? body,
        Type? bodyType)
    {
        if (received.RoutingId is not { } sourceRid
            || received.RequestSeq is not { } requestSeq)
            return;

        var reply = ZLinkEnvelopeCodec.EncodeParts(header, body, bodyType, codecs);
        try
        {
            router.Reply(sourceRid, requestSeq, reply);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(reply);
        }
    }
}
