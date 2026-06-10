using Zlink.Framework.Contracts.Spots;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkActorReply(
    ZlinkStreamCodec codec,
    byte[] payload,
    ZlinkStreamHeaderFlags flags,
    ZlinkStreamMetadata metadata)
{
    public ZlinkStreamCodec Codec { get; } = codec;

    public byte[] Payload { get; } = payload;

    public ZlinkStreamHeaderFlags Flags { get; } = flags;

    public ZlinkStreamMetadata Metadata { get; } = metadata;

    public static ZLinkActorReply FromPayload(
        ZlinkStreamCodec codec,
        byte[] payload)
    {
        return new ZLinkActorReply(codec, payload, ZlinkStreamHeaderFlags.None, ZlinkStreamMetadata.Empty);
    }

    public static ZLinkActorReply FromPayload(
        ZlinkStreamCodec codec,
        byte[] payload,
        ZLinkSpotActorReplyOptionsSnapshot options)
    {
        var flags = ZlinkStreamHeaderFlags.None;
        if (options.CompressPayload)
        {
            payload = ZLinkStreamProtocolDefaults.Lz4Compress(payload).ToArray();
            flags |= ZlinkStreamHeaderFlags.PayloadCompressed;
        }

        var metadata = ZlinkStreamMetadata.Empty;
        foreach (var (key, value) in options.Metadata)
        {
            metadata = metadata.With(key, value);
        }

        return new ZLinkActorReply(codec, payload, flags, metadata);
    }

    public byte[] ToFrame(ZlinkStreamHeader requestHeader)
    {
        if (requestHeader.RequestSeq is not { } requestSeq)
        {
            throw new InvalidOperationException("Actor reply frame requires a request sequence.");
        }

        var responseHeader = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Response,
            Codec,
            Flags | ZlinkStreamHeaderFlags.HasRequestSeq,
            requestSeq,
            requestHeader.Name,
            Metadata);
        return ZLinkStreamFrameCodec.Encode(
            ZLinkStreamProtocolDefaults.EncodeHeader(responseHeader).Span,
            Payload);
    }
}
