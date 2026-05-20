using System.Buffers.Binary;
using System.Text;

namespace Systems.Zlink.Stream.Connector.Runtime.Protocol;

internal sealed class ZlinkStreamHeaderCodec
{
    private const ZlinkStreamHeaderFlags KnownFlags =
        ZlinkStreamHeaderFlags.HasRequestSeq |
        ZlinkStreamHeaderFlags.HasMetadata |
        ZlinkStreamHeaderFlags.PayloadCompressed;

    public ReadOnlyMemory<byte> Encode(ZlinkStreamHeader header)
    {
        ZlinkStreamConnector.ValidateName(header.Name, allowReserved: header.Kind == ZlinkStreamMessageKind.Control);
        ValidateEnum(header.Kind, header.Codec, header.Flags);

        var nameBytes = Encoding.UTF8.GetBytes(header.Name);
        var hasRequestSeq = header.RequestSeq is not null;
        var hasMetadata = header.Metadata.Count > 0;
        var flags = header.Flags;
        ValidateHeaderSemantics(header.Kind, header.Codec, flags, hasRequestSeq, hasMetadata);

        flags = hasRequestSeq ? flags | ZlinkStreamHeaderFlags.HasRequestSeq : flags & ~ZlinkStreamHeaderFlags.HasRequestSeq;
        flags = hasMetadata ? flags | ZlinkStreamHeaderFlags.HasMetadata : flags & ~ZlinkStreamHeaderFlags.HasMetadata;

        var metadataSize = hasMetadata ? ZlinkStreamMetadataCodec.GetPayloadSize(header.Metadata) : 0;
        var size = 3 + (hasRequestSeq ? 8 : 0) + 1 + nameBytes.Length + (hasMetadata ? 2 + metadataSize : 0);
        var buffer = new byte[size];
        var offset = 0;
        buffer[offset++] = (byte)header.Kind;
        buffer[offset++] = (byte)header.Codec;
        buffer[offset++] = (byte)flags;

        if (hasRequestSeq)
        {
            if (header.RequestSeq!.Value.Value == 0)
            {
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.ValidationFailed, "Request sequence must not be zero.");
            }

            BinaryPrimitives.WriteUInt64BigEndian(buffer.AsSpan(offset, 8), header.RequestSeq.Value.Value);
            offset += 8;
        }

        buffer[offset++] = (byte)nameBytes.Length;
        nameBytes.CopyTo(buffer.AsSpan(offset));
        offset += nameBytes.Length;

        if (hasMetadata)
        {
            BinaryPrimitives.WriteUInt16BigEndian(buffer.AsSpan(offset, 2), checked((ushort)metadataSize));
            offset += 2;
            ZlinkStreamMetadataCodec.Write(header.Metadata, buffer.AsSpan(offset, metadataSize));
        }

        return buffer;
    }

    public ZlinkStreamHeader Decode(ReadOnlyMemory<byte> header)
    {
        var span = header.Span;
        if (span.Length < 4)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header is too short.");
        }

        var kind = (ZlinkStreamMessageKind)span[0];
        var codec = (ZlinkStreamCodec)span[1];
        var flags = (ZlinkStreamHeaderFlags)span[2];
        ValidateEnum(kind, codec, flags);

        var offset = 3;
        ZlinkStreamRequestSeq? requestSeq = null;
        if (flags.HasFlag(ZlinkStreamHeaderFlags.HasRequestSeq))
        {
            if (span.Length - offset < 8)
            {
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header request sequence is incomplete.");
            }

            var requestSeqValue = BinaryPrimitives.ReadUInt64BigEndian(span.Slice(offset, 8));
            if (requestSeqValue == 0)
            {
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Request sequence must not be zero.");
            }

            requestSeq = new ZlinkStreamRequestSeq(requestSeqValue);
            offset += 8;
        }

        if (span.Length - offset < 1)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header name length is missing.");
        }

        var nameLength = span[offset++];
        if (nameLength == 0 || span.Length - offset < nameLength)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header packet name is invalid.");
        }

        var name = Encoding.UTF8.GetString(span.Slice(offset, nameLength));
        offset += nameLength;

        var metadata = ZlinkStreamMetadata.Empty;
        if (flags.HasFlag(ZlinkStreamHeaderFlags.HasMetadata))
        {
            if (span.Length - offset < 2)
            {
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header metadata length is missing.");
            }

            var metadataLength = BinaryPrimitives.ReadUInt16BigEndian(span.Slice(offset, 2));
            offset += 2;
            if (span.Length - offset < metadataLength)
            {
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header metadata is incomplete.");
            }

            metadata = ZlinkStreamMetadataCodec.Decode(span.Slice(offset, metadataLength));
            offset += metadataLength;
        }

        if (offset != span.Length)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header contains trailing bytes.");
        }

        ZlinkStreamConnector.ValidateName(name, allowReserved: kind == ZlinkStreamMessageKind.Control);
        ValidateHeaderSemantics(kind, codec, flags, requestSeq is not null, metadata.Count > 0);
        return new ZlinkStreamHeader(kind, codec, flags, requestSeq, name, metadata);
    }

    internal static int GetMetadataPayloadSize(ZlinkStreamMetadata metadata)
        => ZlinkStreamMetadataCodec.GetPayloadSize(metadata);

    private static void ValidateEnum(
        ZlinkStreamMessageKind kind,
        ZlinkStreamCodec codec,
        ZlinkStreamHeaderFlags flags)
    {
        if (!Enum.IsDefined(kind))
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Unknown stream message kind.");
        }

        if (!Enum.IsDefined(codec))
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Unknown stream codec.");
        }

        if ((flags & ~KnownFlags) != 0)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Unknown stream header flag.");
        }
    }

    private static void ValidateHeaderSemantics(
        ZlinkStreamMessageKind kind,
        ZlinkStreamCodec codec,
        ZlinkStreamHeaderFlags flags,
        bool hasRequestSeq,
        bool hasMetadata)
    {
        if (kind == ZlinkStreamMessageKind.Send && hasRequestSeq)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Send packet must not contain a request sequence.");
        }

        if (kind is ZlinkStreamMessageKind.Request or ZlinkStreamMessageKind.Response && !hasRequestSeq)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Request and response packets must contain a request sequence.");
        }

        if (kind == ZlinkStreamMessageKind.Error && codec != ZlinkStreamCodec.Json)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Error packet must use the JSON codec.");
        }

        if (kind == ZlinkStreamMessageKind.Control)
        {
            if (flags != ZlinkStreamHeaderFlags.None)
            {
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Control packet must not contain flags.");
            }

            if (codec != ZlinkStreamCodec.Raw)
            {
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Control packet must use the raw codec.");
            }

            if (hasRequestSeq)
            {
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Control packet must not contain a request sequence.");
            }

            if (hasMetadata)
            {
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Control packet must not contain metadata.");
            }
        }
    }
}
