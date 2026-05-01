namespace Zlink.Framework.Runtime.Streams;

internal sealed record ZLinkActorRelayPacket(
    string ActorId,
    ZLinkStreamHeaderSnapshot StreamHeader,
    bool ExpectsReply,
    byte[] Body)
{
    public static ZLinkActorRelayPacket Create(ZLinkActorRelayEnvelope envelope, byte[] body)
    {
        return new ZLinkActorRelayPacket(
            envelope.ActorId,
            ZLinkStreamHeaderSnapshot.FromHeader(envelope.StreamHeader),
            envelope.ExpectsReply,
            body);
    }

    public ZLinkActorRelayEnvelope ToEnvelope()
    {
        return new ZLinkActorRelayEnvelope(ActorId, StreamHeader.ToHeader(), ExpectsReply);
    }
}

internal sealed record ZLinkStreamHeaderSnapshot(
    ZlinkStreamMessageKind Kind,
    ZlinkStreamCodec Codec,
    ZlinkStreamHeaderFlags Flags,
    ulong? RequestSeq,
    string Name,
    Dictionary<string, string> Metadata)
{
    public static ZLinkStreamHeaderSnapshot FromHeader(ZlinkStreamHeader header)
    {
        return new ZLinkStreamHeaderSnapshot(
            header.Kind,
            header.Codec,
            header.Flags,
            header.RequestSeq?.Value,
            header.Name,
            new Dictionary<string, string>(header.Metadata.Values, StringComparer.Ordinal));
    }

    public ZlinkStreamHeader ToHeader()
    {
        return new ZlinkStreamHeader(
            Kind,
            Codec,
            Flags,
            RequestSeq is { } requestSeq ? new ZlinkStreamRequestSeq(requestSeq) : null,
            Name,
            ZlinkStreamMetadata.Empty.WithMany(Metadata));
    }
}

internal sealed record ZLinkSessionGatewayPacket(
    ZLinkSessionGatewayEnvelope Envelope,
    byte[] Body);
