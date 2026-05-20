using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Messaging;

internal static class ZLinkEnvelopeReplyCompletion
{
    public static void Complete<TReply>(
        RequestResult result,
        IReadOnlyList<Message> reply,
        Action<TReply> complete,
        Action<Exception> fail,
        string operationName)
    {
        try
        {
            if (result != RequestResult.Ok)
            {
                fail(new TimeoutException($"{operationName} failed with result '{result}'."));
                return;
            }

            if (reply.Count == 0)
            {
                fail(new InvalidOperationException($"{operationName} reply is empty."));
                return;
            }

            var replyHeader = ZLinkEnvelopeCodec.DecodeHeader(reply);
            if (replyHeader.Kind == ZLinkMessageKind.Error)
            {
                fail(new InvalidOperationException(replyHeader.ErrorMessage ?? $"{operationName} failed."));
                return;
            }

            complete((TReply?)ZLinkEnvelopeCodec.DecodeBody(reply, typeof(TReply))
                ?? throw new InvalidOperationException($"{operationName} reply body is null."));
        }
        catch (Exception exception)
        {
            fail(exception);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(reply);
        }
    }
}
