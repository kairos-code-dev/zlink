namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkSpotReplyEnvelope
{
    public static IReadOnlyList<Message> EncodeResponseParts(
        string channelName,
        string messageName,
        string? correlationId,
        object? reply,
        Type? replyType)
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
        return ZLinkEnvelopeCodec.EncodeParts(replyHeader, reply, replyType);
    }
}
