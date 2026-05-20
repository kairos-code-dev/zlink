using System.Buffers.Binary;
using System.Collections.Concurrent;
using System.Reflection;
using System.Text;
using K4os.Compression.LZ4;

namespace Zlink.Framework.Runtime.Streams;

internal static class ZLinkStreamProtocolDefaults
{
    public static IZlinkStreamPacketNameResolver PacketNameResolver { get; } = new DefaultPacketNameResolver();

    public static ReadOnlyMemory<byte> EncodeHeader(ZlinkStreamHeader header)
        => DefaultHeaderCodec.Encode(header);

    public static ZlinkStreamHeader DecodeHeader(ReadOnlyMemory<byte> header)
        => DefaultHeaderCodec.Decode(header);

    public static ReadOnlyMemory<byte> Lz4Compress(ReadOnlyMemory<byte> payload)
        => LZ4Pickler.Pickle(payload.Span);

    public static ReadOnlyMemory<byte> Lz4Decompress(ReadOnlyMemory<byte> payload)
        => LZ4Pickler.Unpickle(payload.Span);

    private static class DefaultHeaderCodec
    {
        private const ZlinkStreamHeaderFlags KnownFlags =
            ZlinkStreamHeaderFlags.HasRequestSeq |
            ZlinkStreamHeaderFlags.HasMetadata |
            ZlinkStreamHeaderFlags.PayloadCompressed;

        public static ReadOnlyMemory<byte> Encode(ZlinkStreamHeader header)
        {
            ValidateName(header.Name, allowReserved: header.Kind == ZlinkStreamMessageKind.Control);
            ValidateEnum(header.Kind, header.Codec, header.Flags);

            var nameBytes = Encoding.UTF8.GetBytes(header.Name);
            var hasRequestSeq = header.RequestSeq is not null;
            var hasMetadata = header.Metadata.Count > 0;
            var flags = header.Flags;
            ValidateHeaderSemantics(header.Kind, header.Codec, flags, hasRequestSeq, hasMetadata);

            flags = hasRequestSeq ? flags | ZlinkStreamHeaderFlags.HasRequestSeq : flags & ~ZlinkStreamHeaderFlags.HasRequestSeq;
            flags = hasMetadata ? flags | ZlinkStreamHeaderFlags.HasMetadata : flags & ~ZlinkStreamHeaderFlags.HasMetadata;

            var metadataSize = hasMetadata ? MetadataCodec.GetPayloadSize(header.Metadata) : 0;
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
                    throw Error(ZlinkStreamErrorCode.ValidationFailed, "Request sequence must not be zero.");
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
                MetadataCodec.Write(header.Metadata, buffer.AsSpan(offset, metadataSize));
            }

            return buffer;
        }

        public static ZlinkStreamHeader Decode(ReadOnlyMemory<byte> header)
        {
            var span = header.Span;
            if (span.Length < 4)
            {
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header is too short.");
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
                    throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header request sequence is incomplete.");
                }

                var requestSeqValue = BinaryPrimitives.ReadUInt64BigEndian(span.Slice(offset, 8));
                if (requestSeqValue == 0)
                {
                    throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Request sequence must not be zero.");
                }

                requestSeq = new ZlinkStreamRequestSeq(requestSeqValue);
                offset += 8;
            }

            if (span.Length - offset < 1)
            {
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header name length is missing.");
            }

            var nameLength = span[offset++];
            if (nameLength == 0 || span.Length - offset < nameLength)
            {
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header packet name is invalid.");
            }

            var name = Encoding.UTF8.GetString(span.Slice(offset, nameLength));
            offset += nameLength;

            var metadata = ZlinkStreamMetadata.Empty;
            if (flags.HasFlag(ZlinkStreamHeaderFlags.HasMetadata))
            {
                if (span.Length - offset < 2)
                {
                    throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header metadata length is missing.");
                }

                var metadataLength = BinaryPrimitives.ReadUInt16BigEndian(span.Slice(offset, 2));
                offset += 2;
                if (span.Length - offset < metadataLength)
                {
                    throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header metadata is incomplete.");
                }

                metadata = MetadataCodec.Decode(span.Slice(offset, metadataLength));
                offset += metadataLength;
            }

            if (offset != span.Length)
            {
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Helper header contains trailing bytes.");
            }

            ValidateName(name, allowReserved: kind == ZlinkStreamMessageKind.Control);
            ValidateHeaderSemantics(kind, codec, flags, requestSeq is not null, metadata.Count > 0);
            return new ZlinkStreamHeader(kind, codec, flags, requestSeq, name, metadata);
        }

        private static void ValidateEnum(
            ZlinkStreamMessageKind kind,
            ZlinkStreamCodec codec,
            ZlinkStreamHeaderFlags flags)
        {
            if (!Enum.IsDefined(kind))
            {
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Unknown stream message kind.");
            }

            if (!Enum.IsDefined(codec))
            {
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Unknown stream codec.");
            }

            if ((flags & ~KnownFlags) != 0)
            {
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Unknown stream header flag.");
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
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Send packet must not contain a request sequence.");
            }

            if (kind is ZlinkStreamMessageKind.Request or ZlinkStreamMessageKind.Response && !hasRequestSeq)
            {
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Request and response packets must contain a request sequence.");
            }

            if (kind == ZlinkStreamMessageKind.Error && codec != ZlinkStreamCodec.Json)
            {
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Error packet must use the JSON codec.");
            }

            if (kind == ZlinkStreamMessageKind.Control)
            {
                if (flags != ZlinkStreamHeaderFlags.None)
                {
                    throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Control packet must not contain flags.");
                }

                if (codec != ZlinkStreamCodec.Raw)
                {
                    throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Control packet must use the raw codec.");
                }

                if (hasRequestSeq)
                {
                    throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Control packet must not contain a request sequence.");
                }

                if (hasMetadata)
                {
                    throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Control packet must not contain metadata.");
                }
            }
        }
    }

    private sealed class DefaultPacketNameResolver : IZlinkStreamPacketNameResolver
    {
        private static readonly ConcurrentDictionary<Type, string> Cache = new();

        public string Resolve(Type payloadType)
        {
            ArgumentNullException.ThrowIfNull(payloadType);
            return Cache.GetOrAdd(payloadType, static type =>
                type.GetCustomAttribute<ZlinkStreamPacketNameAttribute>(inherit: false)?.Name
                ?? type.Name);
        }
    }

    private static class MetadataCodec
    {
        public static int GetPayloadSize(ZlinkStreamMetadata metadata)
            => metadata.Count == 0 ? 0 : CalculatePayloadSize(metadata);

        public static void Write(ZlinkStreamMetadata metadata, Span<byte> destination)
        {
            var offset = 0;
            destination[offset++] = (byte)metadata.Count;
            foreach (var (key, value) in metadata.Values)
            {
                var keyLength = Encoding.UTF8.GetByteCount(key);
                var valueLength = Encoding.UTF8.GetByteCount(value);

                destination[offset++] = (byte)keyLength;
                offset += Encoding.UTF8.GetBytes(key, destination.Slice(offset, keyLength));
                BinaryPrimitives.WriteUInt16BigEndian(destination.Slice(offset, 2), (ushort)valueLength);
                offset += 2;
                offset += Encoding.UTF8.GetBytes(value, destination.Slice(offset, valueLength));
            }
        }

        public static ZlinkStreamMetadata Decode(ReadOnlySpan<byte> metadata)
        {
            if (metadata.Length == 0)
            {
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Metadata payload is empty.");
            }

            var offset = 0;
            var count = metadata[offset++];
            var values = new Dictionary<string, string>(StringComparer.Ordinal);
            for (var i = 0; i < count; i++)
            {
                var key = DecodeString(metadata, ref offset, byteLength: true, "key");
                var value = DecodeString(metadata, ref offset, byteLength: false, "value");

                if (!values.TryAdd(key, value))
                {
                    throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Duplicate metadata key.");
                }
            }

            if (offset != metadata.Length)
            {
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, "Metadata contains trailing bytes.");
            }

            return ZlinkStreamMetadata.Empty.WithMany(values);
        }

        private static int CalculatePayloadSize(ZlinkStreamMetadata metadata)
        {
            if (metadata.Count > byte.MaxValue)
            {
                throw Error(ZlinkStreamErrorCode.ValidationFailed, "Metadata entry count must not exceed 255.");
            }

            var size = 1;
            foreach (var (key, value) in metadata.Values)
            {
                var keyLength = Encoding.UTF8.GetByteCount(key);
                var valueLength = Encoding.UTF8.GetByteCount(value);
                if (keyLength is 0 or > byte.MaxValue)
                {
                    throw Error(ZlinkStreamErrorCode.ValidationFailed, "Metadata key length is invalid.");
                }

                if (valueLength > ushort.MaxValue)
                {
                    throw Error(ZlinkStreamErrorCode.ValidationFailed, "Metadata value is too large.");
                }

                size = checked(size + 1 + keyLength + 2 + valueLength);
            }

            return size;
        }

        private static string DecodeString(
            ReadOnlySpan<byte> metadata,
            ref int offset,
            bool byteLength,
            string name)
        {
            var length = byteLength
                ? ReadByteLength(metadata, ref offset, name)
                : ReadUInt16Length(metadata, ref offset, name);
            if (length == 0 || metadata.Length - offset < length)
            {
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, $"Metadata {name} is invalid.");
            }

            var value = Encoding.UTF8.GetString(metadata.Slice(offset, length));
            offset += length;
            return value;
        }

        private static int ReadByteLength(ReadOnlySpan<byte> metadata, ref int offset, string name)
        {
            if (metadata.Length - offset < 1)
            {
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, $"Metadata {name} length is missing.");
            }

            return metadata[offset++];
        }

        private static int ReadUInt16Length(ReadOnlySpan<byte> metadata, ref int offset, string name)
        {
            if (metadata.Length - offset < 2)
            {
                throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, $"Metadata {name} length is missing.");
            }

            var length = BinaryPrimitives.ReadUInt16BigEndian(metadata.Slice(offset, 2));
            offset += 2;
            return length;
        }
    }

    private static void ValidateName(string name, bool allowReserved)
    {
        if (string.IsNullOrEmpty(name))
        {
            throw Error(ZlinkStreamErrorCode.ValidationFailed, "Message name must not be empty.");
        }

        if (!allowReserved && name.StartsWith("__zlink.", StringComparison.Ordinal))
        {
            throw Error(ZlinkStreamErrorCode.ValidationFailed, "Message name uses a reserved zlink prefix.");
        }

        if (Encoding.UTF8.GetByteCount(name) > byte.MaxValue)
        {
            throw Error(ZlinkStreamErrorCode.ValidationFailed, "Message name must not exceed 255 UTF-8 bytes.");
        }
    }

    private static ZlinkStreamException Error(ZlinkStreamErrorCode code, string message)
        => new(new ZlinkStreamError(code, message));
}
