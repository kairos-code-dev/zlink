using System.Buffers.Binary;
using System.Collections.Concurrent;

using System.Net.Security;

using System.Net.Sockets;
using System.Net.WebSockets;
using System.Security.Authentication;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Text.Json;
using System.Threading.Channels;

using K4os.Compression.LZ4;

namespace Systems.Zlink.Stream.Connector.Protocol;

public sealed class ZlinkStreamHeaderCodec : IZlinkStreamHeaderCodec
{
    private const ZlinkStreamHeaderFlags KnownFlags =
        ZlinkStreamHeaderFlags.HasRequestSeq |
        ZlinkStreamHeaderFlags.HasMetadata |
        ZlinkStreamHeaderFlags.BodyCompressed;

    public ReadOnlyMemory<byte> Encode(ZlinkStreamHeader header)
    {
        ZlinkStreamConnector.ValidateName(header.Name);
        ValidateEnum(header.Kind, header.Codec, header.Flags);

        var nameBytes = Encoding.UTF8.GetBytes(header.Name);
        var hasRequestSeq = header.RequestSeq is not null;
        var hasMetadata = header.Metadata.Count > 0;
        var flags = header.Flags;
        ValidateHeaderSemantics(header.Kind, header.Codec, hasRequestSeq);

        flags = hasRequestSeq ? flags | ZlinkStreamHeaderFlags.HasRequestSeq : flags & ~ZlinkStreamHeaderFlags.HasRequestSeq;
        flags = hasMetadata ? flags | ZlinkStreamHeaderFlags.HasMetadata : flags & ~ZlinkStreamHeaderFlags.HasMetadata;

        var metadataBytes = hasMetadata ? EncodeMetadata(header.Metadata) : Array.Empty<byte>();
        var size = 3 + (hasRequestSeq ? 8 : 0) + 1 + nameBytes.Length + (hasMetadata ? 2 + metadataBytes.Length : 0);
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
            BinaryPrimitives.WriteUInt16BigEndian(buffer.AsSpan(offset, 2), checked((ushort)metadataBytes.Length));
            offset += 2;
            metadataBytes.CopyTo(buffer.AsSpan(offset));
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
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header message name is invalid.");
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

            metadata = DecodeMetadata(span.Slice(offset, metadataLength));
            offset += metadataLength;
        }

        if (offset != span.Length)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header contains trailing bytes.");
        }

        ValidateHeaderSemantics(kind, codec, requestSeq is not null);
        return new ZlinkStreamHeader(kind, codec, flags, requestSeq, name, metadata);
    }

    internal static int GetMetadataPayloadSize(ZlinkStreamMetadata metadata)
        => metadata.Count == 0 ? 0 : EncodeMetadata(metadata).Length;

    private static byte[] EncodeMetadata(ZlinkStreamMetadata metadata)
    {
        if (metadata.Count > byte.MaxValue)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.ValidationFailed, "Metadata entry count must not exceed 255.");
        }

        using var stream = new System.IO.MemoryStream();
        stream.WriteByte((byte)metadata.Count);
        foreach (var (key, value) in metadata.Values)
        {
            var keyBytes = Encoding.UTF8.GetBytes(key);
            var valueBytes = Encoding.UTF8.GetBytes(value);
            if (keyBytes.Length is 0 or > byte.MaxValue)
            {
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.ValidationFailed, "Metadata key length is invalid.");
            }

            if (valueBytes.Length > ushort.MaxValue)
            {
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.ValidationFailed, "Metadata value is too large.");
            }

            stream.WriteByte((byte)keyBytes.Length);
            stream.Write(keyBytes);
            var length = new byte[2];
            BinaryPrimitives.WriteUInt16BigEndian(length, (ushort)valueBytes.Length);
            stream.Write(length);
            stream.Write(valueBytes);
        }

        return stream.ToArray();
    }

    private static ZlinkStreamMetadata DecodeMetadata(ReadOnlySpan<byte> metadata)
    {
        if (metadata.Length == 0)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Metadata payload is empty.");
        }

        var offset = 0;
        var count = metadata[offset++];
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var i = 0; i < count; i++)
        {
            if (metadata.Length - offset < 1)
            {
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Metadata key length is missing.");
            }

            var keyLength = metadata[offset++];
            if (keyLength == 0 || metadata.Length - offset < keyLength)
            {
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Metadata key is invalid.");
            }

            var key = Encoding.UTF8.GetString(metadata.Slice(offset, keyLength));
            offset += keyLength;

            if (metadata.Length - offset < 2)
            {
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Metadata value length is missing.");
            }

            var valueLength = BinaryPrimitives.ReadUInt16BigEndian(metadata.Slice(offset, 2));
            offset += 2;
            if (metadata.Length - offset < valueLength)
            {
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Metadata value is incomplete.");
            }

            var value = Encoding.UTF8.GetString(metadata.Slice(offset, valueLength));
            offset += valueLength;

            if (!values.TryAdd(key, value))
            {
                throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Duplicate metadata key.");
            }
        }

        if (offset != metadata.Length)
        {
            throw ZlinkStreamConnector.Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Metadata contains trailing bytes.");
        }

        return ZlinkStreamMetadata.FromDictionary(values);
    }

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
        bool hasRequestSeq)
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
    }
}
