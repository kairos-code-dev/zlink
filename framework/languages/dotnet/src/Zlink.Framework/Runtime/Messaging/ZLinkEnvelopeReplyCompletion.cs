namespace Zlink.Framework.Runtime.Messaging;

internal static class ZLinkEnvelopeReplyCompletion
{
    public static void Complete<TReply>(
        RequestResult result,
        IReadOnlyList<Message> reply,
        Action<TReply> complete,
        Action<Exception> fail,
        string operationName,
        ZLinkCodecRegistryBuilder? codecs = null)
    {
        try
        {
            if (result != RequestResult.Ok)
            {
                fail(ZLinkRequestFailureMapper.CreateCompletionException(result, operationName));
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

            complete((TReply?)ZLinkEnvelopeCodec.DecodeBody(reply, typeof(TReply), replyHeader.ContentType, codecs)
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
