
namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotRouteDispatcher(
    string channelName,
    ZLinkSpotPacketRegistry packets,
    Func<ZLinkSpotHandlerInvoker> handlerInvoker)
{
    public async ValueTask DispatchAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        using (received)
        {
            if (received.Parts.Count == 0)
            {
                return;
            }

            var header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts);
            if (!packets.TryResolve(header, out var descriptor) || descriptor is null)
            {
                return;
            }

            var message = ZLinkEnvelopeCodec.DecodeBody(received.Parts, descriptor.MessageType);
            if (!descriptor.IsRequest)
            {
                await handlerInvoker()
                    .InvokePacketAsync(descriptor, message, cancellationToken)
                    .ConfigureAwait(false);
                return;
            }

            var reply = await handlerInvoker()
                .InvokeRequestAsync(descriptor, message, cancellationToken)
                .ConfigureAwait(false);
            var replyParts = ZLinkSpotReplyEnvelope.EncodeResponseParts(
                channelName,
                descriptor.MessageName,
                header.CorrelationId,
                reply,
                descriptor.ReplyType);
            try
            {
                received.Reply()
                    .Message(replyParts[0])
                    .Message(replyParts[1])
                    .Submit();
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(replyParts);
            }
        }
    }
}
