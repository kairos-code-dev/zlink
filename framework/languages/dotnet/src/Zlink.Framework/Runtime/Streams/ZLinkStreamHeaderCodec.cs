using System.Buffers.Binary;
using System.Text;

namespace Zlink.Framework.Runtime.Streams;

internal static class ZLinkStreamHeaderCodec
{
    // Keep byte-compatible with Systems.Zlink.Stream.Connector headers; StreamWireInteropTests is the drift gate.
    private const ZlinkStreamHeaderFlags KnownFlags =
        ZlinkStreamHeaderFlags.HasRequestSeq |
        ZlinkStreamHeaderFlags.HasMetadata |
        ZlinkStreamHeaderFlags.PayloadCompressed |
        ZlinkStreamHeaderFlags.HasCorrelationId;

    public static ReadOnlyMemory<byte> Encode(ZlinkStreamHeader header)
    {
        ValidateName(header.Name, header.Kind == ZlinkStreamMessageKind.Control);
        ValidateEnum(header.Kind, header.Codec, header.Flags);

        var nameBytes = Encoding.UTF8.GetBytes(header.Name);
        var hasRequestSeq = header.RequestSeq is not null;
        var hasMetadata = header.Metadata.Count > 0;
        var hasCorrelationId = !string.IsNullOrEmpty(header.CorrelationId);
        var correlationBytes = hasCorrelationId
            ? Encoding.UTF8.GetBytes(header.CorrelationId!)
            : Array.Empty<byte>();
        if (correlationBytes.Length > byte.MaxValue)
            throw Error(ZlinkStreamErrorCode.ValidationFailed, "Correlation id is too long.");

        var flags = header.Flags;
        ValidateHeaderSemantics(header.Kind, header.Codec, flags, hasRequestSeq, hasMetadata, hasCorrelationId);

        flags = hasRequestSeq
            ? flags | ZlinkStreamHeaderFlags.HasRequestSeq
            : flags & ~ZlinkStreamHeaderFlags.HasRequestSeq;
        flags = hasMetadata
            ? flags | ZlinkStreamHeaderFlags.HasMetadata
            : flags & ~ZlinkStreamHeaderFlags.HasMetadata;
        flags = hasCorrelationId
            ? flags | ZlinkStreamHeaderFlags.HasCorrelationId
            : flags & ~ZlinkStreamHeaderFlags.HasCorrelationId;

        var metadataSize = hasMetadata
            ? ZLinkStreamMetadataCodec.GetPayloadSize(header.Metadata)
            : 0;
        var size = 3
                   + (hasRequestSeq ? sizeof(ulong) : 0)
                   + 1 + nameBytes.Length
                   + (hasMetadata ? sizeof(ushort) + metadataSize : 0)
                   + (hasCorrelationId ? 1 + correlationBytes.Length : 0);
        var buffer = new byte[size];
        var offset = 0;
        buffer[offset++] = (byte)header.Kind;
        buffer[offset++] = (byte)header.Codec;
        buffer[offset++] = (byte)flags;

        if (hasRequestSeq)
        {
            if (header.RequestSeq!.Value.Value == 0)
                throw Error(ZlinkStreamErrorCode.ValidationFailed, "Request sequence must not be zero.");

            BinaryPrimitives.WriteUInt64BigEndian(
                buffer.AsSpan(offset, sizeof(ulong)),
                header.RequestSeq.Value.Value);
            offset += sizeof(ulong);
        }

        buffer[offset++] = (byte)nameBytes.Length;
        nameBytes.CopyTo(buffer.AsSpan(offset));
        offset += nameBytes.Length;

        if (hasMetadata)
        {
            BinaryPrimitives.WriteUInt16BigEndian(
                buffer.AsSpan(offset, sizeof(ushort)),
                checked((ushort)metadataSize));
            offset += sizeof(ushort);
            ZLinkStreamMetadataCodec.Write(
                header.Metadata,
                buffer.AsSpan(offset, metadataSize));
            offset += metadataSize;
        }

        if (hasCorrelationId)
        {
            buffer[offset++] = (byte)correlationBytes.Length;
            correlationBytes.CopyTo(buffer.AsSpan(offset));
            offset += correlationBytes.Length;
        }

        return buffer;
    }

    public static ZlinkStreamHeader Decode(ReadOnlyMemory<byte> header)
    {
        var span = header.Span;
        if (span.Length < 4) throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header is too short.");

        var kind = (ZlinkStreamMessageKind)span[0];
        var codec = (ZlinkStreamCodec)span[1];
        var flags = (ZlinkStreamHeaderFlags)span[2];
        ValidateEnum(kind, codec, flags);

        var offset = 3;
        ZlinkStreamRequestSeq? requestSeq = null;
        if (flags.HasFlag(ZlinkStreamHeaderFlags.HasRequestSeq))
        {
            if (span.Length - offset < sizeof(ulong))
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header request sequence is incomplete.");

            var requestSeqValue = BinaryPrimitives.ReadUInt64BigEndian(
                span.Slice(offset, sizeof(ulong)));
            if (requestSeqValue == 0)
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Request sequence must not be zero.");

            requestSeq = new ZlinkStreamRequestSeq(requestSeqValue);
            offset += sizeof(ulong);
        }

        if (span.Length - offset < 1)
            throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header name length is missing.");

        var nameLength = span[offset++];
        if (nameLength == 0 || span.Length - offset < nameLength)
            throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header packet name is invalid.");

        var name = Encoding.UTF8.GetString(span.Slice(offset, nameLength));
        offset += nameLength;

        var metadata = ZlinkStreamMetadata.Empty;
        if (flags.HasFlag(ZlinkStreamHeaderFlags.HasMetadata))
        {
            if (span.Length - offset < sizeof(ushort))
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header metadata length is missing.");

            var metadataLength = BinaryPrimitives.ReadUInt16BigEndian(
                span.Slice(offset, sizeof(ushort)));
            offset += sizeof(ushort);
            if (span.Length - offset < metadataLength)
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header metadata is incomplete.");

            metadata = ZLinkStreamMetadataCodec.Decode(span.Slice(offset, metadataLength));
            offset += metadataLength;
        }

        string? correlationId = null;
        if (flags.HasFlag(ZlinkStreamHeaderFlags.HasCorrelationId))
        {
            if (span.Length - offset < 1)
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header correlation id length is missing.");

            var correlationLength = span[offset++];
            if (correlationLength == 0 || span.Length - offset < correlationLength)
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header correlation id is invalid.");

            correlationId = Encoding.UTF8.GetString(span.Slice(offset, correlationLength));
            offset += correlationLength;
        }

        if (offset != span.Length)
            throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header contains trailing bytes.");

        ValidateName(name, kind == ZlinkStreamMessageKind.Control);
        ValidateHeaderSemantics(kind, codec, flags, requestSeq is not null, metadata.Count > 0,
            correlationId is not null);
        return new ZlinkStreamHeader(kind, codec, flags, requestSeq, name, metadata, correlationId);
    }

    private static void ValidateEnum(
        ZlinkStreamMessageKind kind,
        ZlinkStreamCodec codec,
        ZlinkStreamHeaderFlags flags)
    {
        if (!Enum.IsDefined(kind)) throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Unknown stream message kind.");

        if (!Enum.IsDefined(codec)) throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Unknown stream codec.");

        if ((flags & ~KnownFlags) != 0)
            throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Unknown stream header flag.");
    }

    private static void ValidateHeaderSemantics(
        ZlinkStreamMessageKind kind,
        ZlinkStreamCodec codec,
        ZlinkStreamHeaderFlags flags,
        bool hasRequestSeq,
        bool hasMetadata,
        bool hasCorrelationId)
    {
        if (kind == ZlinkStreamMessageKind.Send && hasRequestSeq)
            throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Send packet must not contain a request sequence.");

        if (kind is ZlinkStreamMessageKind.Request or ZlinkStreamMessageKind.Response && !hasRequestSeq)
            throw Error(ZlinkStreamErrorCode.FrameDecodeFailed,
                "Request and response packets must contain a request sequence.");

        if (kind == ZlinkStreamMessageKind.Error && codec != ZlinkStreamCodec.Json)
            throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Error packet must use the JSON codec.");

        if (kind == ZlinkStreamMessageKind.Control)
        {
            if (flags != ZlinkStreamHeaderFlags.None)
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Control packet must not contain flags.");

            if (codec != ZlinkStreamCodec.Raw)
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Control packet must use the raw codec.");

            if (hasRequestSeq)
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed,
                    "Control packet must not contain a request sequence.");

            if (hasMetadata)
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Control packet must not contain metadata.");

            if (hasCorrelationId)
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed,
                    "Control packet must not contain a correlation id.");
        }
    }

    private static void ValidateName(string name, bool allowReserved)
    {
        if (string.IsNullOrEmpty(name))
            throw Error(ZlinkStreamErrorCode.ValidationFailed, "Message name must not be empty.");

        if (!allowReserved && name.StartsWith("__zlink.", StringComparison.Ordinal))
            throw Error(ZlinkStreamErrorCode.ValidationFailed, "Message name uses a reserved zlink prefix.");

        if (Encoding.UTF8.GetByteCount(name) > byte.MaxValue)
            throw Error(ZlinkStreamErrorCode.ValidationFailed, "Message name must not exceed 255 UTF-8 bytes.");
    }

    private static ZlinkStreamException Error(ZlinkStreamErrorCode code, string message)
    {
        return new ZlinkStreamException(new ZlinkStreamError(code, message));
    }
}
