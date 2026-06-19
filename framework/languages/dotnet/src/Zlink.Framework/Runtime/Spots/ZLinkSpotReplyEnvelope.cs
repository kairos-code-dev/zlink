namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkSpotReplyEnvelope
{
    public static IReadOnlyList<Message> EncodeResponseParts(
        string channelName,
        string messageName,
        string? correlationId,
        object? reply,
        Type? replyType,
        ZLinkCodecRegistryBuilder? codecs = null)
    {
        var replyHeader = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Response,
            channelName,
            messageName,
            ZLinkEnvelopeCodec.DefaultContentType,
            correlationId,
            null,
            null,
            null,
            null);
        return ZLinkEnvelopeCodec.EncodeParts(replyHeader, reply, replyType, codecs);
    }

    public static IReadOnlyList<Message> EncodeErrorParts(
        string channelName,
        string messageName,
        string? correlationId,
        Exception exception)
    {
        var replyHeader = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Error,
            channelName,
            messageName,
            ZLinkEnvelopeCodec.DefaultContentType,
            correlationId,
            null,
            null,
            exception.GetType().Name,
            exception.Message);
        return ZLinkEnvelopeCodec.EncodeParts(replyHeader, null, null);
    }
}
