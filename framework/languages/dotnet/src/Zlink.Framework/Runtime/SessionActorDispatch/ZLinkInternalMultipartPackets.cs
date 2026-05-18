namespace Zlink.Framework.Runtime.SessionActorDispatch;

internal static class ZLinkInternalMultipartPackets
{
    private static readonly IZlinkStreamHeaderCodec HeaderCodec = ZlinkStreamDefaultCodecs.Header();

    public static IReadOnlyList<Message> CreateActorDispatchParts(
        string actorId,
        string actorType,
        ZlinkStreamHeader streamHeader,
        ReadOnlySpan<byte> body)
    {
        return
        [
            ZLinkEnvelopeCodec.EncodePart(new ZLinkActorDispatchMetadata(actorId, actorType)),
            Message.FromBytes(HeaderCodec.Encode(streamHeader).Span),
            Message.FromBytes(body)
        ];
    }

    public static IReadOnlyList<Message> CreateActorDispatchParts(
        string actorId,
        string actorType,
        ZlinkStreamHeader streamHeader,
        object? body,
        Type bodyType)
    {
        return CreateActorDispatchParts(
            actorId,
            actorType,
            streamHeader,
            ZLinkEnvelopeCodec.EncodeJsonBytes(body, bodyType));
    }

    public static IReadOnlyList<Message> CreateSessionProxyParts(
        ZLinkSessionProxyEnvelope envelope,
        object? body,
        Type bodyType)
    {
        return
        [
            ZLinkEnvelopeCodec.EncodePart(envelope),
            ZLinkEnvelopeCodec.EncodeJsonPart(body, bodyType)
        ];
    }

    public static IReadOnlyList<Message> CreateSessionDisconnectParts(
        ZLinkSessionDisconnectEnvelope envelope)
    {
        return
        [
            ZLinkEnvelopeCodec.EncodePart(envelope)
        ];
    }

    public static IReadOnlyList<Message> CreateActorDisconnectedParts(
        string actorId,
        string actorType)
    {
        return
        [
            ZLinkEnvelopeCodec.EncodePart(new ZLinkActorDispatchMetadata(actorId, actorType))
        ];
    }

    public static ZLinkActorDispatchMetadata DecodeActorDispatchMetadata(Received received)
    {
        EnsurePartCount(received, 4, "actor dispatch");
        return ZLinkEnvelopeCodec.DecodePart<ZLinkActorDispatchMetadata>(received.Parts[1]);
    }

    public static ZlinkStreamHeader DecodeActorDispatchStreamHeader(Received received)
    {
        EnsurePartCount(received, 4, "actor dispatch");
        return HeaderCodec.Decode(received.Parts[2].AsReadOnlyMemory());
    }

    public static Message DecodeActorDispatchBody(Received received)
    {
        EnsurePartCount(received, 4, "actor dispatch");
        return Message.FromBytes(received.Parts[3].AsReadOnlySpan());
    }

    public static ZLinkActorDispatchMetadata DecodeActorDisconnectedMetadata(Received received)
    {
        EnsurePartCount(received, 2, "actor disconnected");
        return ZLinkEnvelopeCodec.DecodePart<ZLinkActorDispatchMetadata>(received.Parts[1]);
    }

    public static ZLinkSessionProxyEnvelope DecodeSessionProxyEnvelope(Received received)
    {
        EnsurePartCount(received, 3, "session proxy");
        return ZLinkEnvelopeCodec.DecodePart<ZLinkSessionProxyEnvelope>(received.Parts[1]);
    }

    public static ZLinkSessionDisconnectEnvelope DecodeSessionDisconnectEnvelope(Received received)
    {
        EnsurePartCount(received, 2, "session disconnect");
        return ZLinkEnvelopeCodec.DecodePart<ZLinkSessionDisconnectEnvelope>(received.Parts[1]);
    }

    public static ReadOnlyMemory<byte> DecodeSessionProxyBody(Received received)
    {
        EnsurePartCount(received, 3, "session proxy");
        return received.Parts[2].AsReadOnlyMemory();
    }

    private static void EnsurePartCount(Received received, int minimumCount, string packetName)
    {
        if (received.Parts.Count < minimumCount)
        {
            throw new InvalidOperationException(
                $"Internal routed {packetName} packet requires at least {minimumCount} message parts.");
        }
    }
}
