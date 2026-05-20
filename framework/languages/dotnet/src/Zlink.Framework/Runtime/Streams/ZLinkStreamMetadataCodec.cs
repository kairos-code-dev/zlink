using System.Buffers.Binary;
using System.Text;

namespace Zlink.Framework.Runtime.Streams;

internal static class ZLinkStreamMetadataCodec
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
            BinaryPrimitives.WriteUInt16BigEndian(destination.Slice(offset, sizeof(ushort)), (ushort)valueLength);
            offset += sizeof(ushort);
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

            size = checked(size + 1 + keyLength + sizeof(ushort) + valueLength);
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
        if (metadata.Length - offset < sizeof(ushort))
        {
            throw Error(ZlinkStreamErrorCode.FrameDecodeFailed, $"Metadata {name} length is missing.");
        }

        var length = BinaryPrimitives.ReadUInt16BigEndian(metadata.Slice(offset, sizeof(ushort)));
        offset += sizeof(ushort);
        return length;
    }

    private static ZlinkStreamException Error(ZlinkStreamErrorCode code, string message)
        => new(new ZlinkStreamError(code, message));
}
