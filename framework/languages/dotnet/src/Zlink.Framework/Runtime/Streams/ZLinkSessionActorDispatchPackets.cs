namespace Zlink.Framework.Runtime.Streams;

internal static class ZLinkInternalPacketNames
{
    public const string ActorDispatch = "ZLink.ActorDispatch";

    public const string SessionProxy = "ZLink.SessionProxy";
}

internal sealed record ZLinkActorDispatchPacket(
    string ActorId,
    string ActorType,
    ZLinkStreamHeaderSnapshot StreamHeader,
    byte[] Body);

internal sealed record ZLinkSessionProxyPacket(
    ZLinkSessionProxyEnvelope Envelope,
    byte[] Body);

internal sealed record ZLinkSessionProxyEnvelope(
    string ActorId,
    string BindingToken,
    string PacketName,
    bool ExpectsReply,
    Dictionary<string, string> Metadata);

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
