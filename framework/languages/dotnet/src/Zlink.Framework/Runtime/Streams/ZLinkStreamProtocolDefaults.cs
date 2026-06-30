using System.Collections.Concurrent;
using System.Reflection;
using K4os.Compression.LZ4;

namespace Zlink.Framework.Runtime.Streams;

internal static class ZLinkStreamProtocolDefaults
{
    private const int DefaultMaxDecompressedPayloadSize = 64 * 1024;

    public static IZlinkStreamPacketNameResolver PacketNameResolver { get; } = new DefaultPacketNameResolver();

    public static ReadOnlyMemory<byte> EncodeHeader(ZlinkStreamHeader header)
    {
        return ZLinkStreamHeaderCodec.Encode(header);
    }

    public static ZlinkStreamHeader DecodeHeader(ReadOnlyMemory<byte> header)
    {
        return ZLinkStreamHeaderCodec.Decode(header);
    }

    public static ReadOnlyMemory<byte> Lz4Compress(ReadOnlyMemory<byte> payload)
    {
        return LZ4Pickler.Pickle(payload.Span);
    }

    public static ReadOnlyMemory<byte> Lz4Decompress(ReadOnlyMemory<byte> payload)
    {
        return Lz4Decompress(payload, DefaultMaxDecompressedPayloadSize);
    }

    public static ReadOnlyMemory<byte> Lz4Decompress(
        ReadOnlyMemory<byte> payload,
        int maxDecompressedPayloadSize)
    {
        var decompressedSize = LZ4Pickler.UnpickledSize(payload.Span);
        if (decompressedSize > maxDecompressedPayloadSize)
            throw new InvalidOperationException("LZ4 decoded stream payload exceeds maximum stream payload size.");

        return LZ4Pickler.Unpickle(payload.Span);
    }

    public static IZlinkStreamCompressionCodec CreateLz4CompressionCodec()
    {
        return new Lz4CompressionCodec();
    }

    public static ReadOnlyMemory<byte> Compress(
        IZlinkStreamCompressionCodec? compressionCodec,
        ReadOnlyMemory<byte> payload)
    {
        if (compressionCodec is null) throw new InvalidOperationException("Compression codec is not configured.");

        return compressionCodec.Compress(payload);
    }

    public static ReadOnlyMemory<byte> Decompress(
        IZlinkStreamCompressionCodec? compressionCodec,
        ReadOnlyMemory<byte> payload,
        int maxDecompressedPayloadSize = DefaultMaxDecompressedPayloadSize)
    {
        if (compressionCodec is null) throw new InvalidOperationException("Compression codec is not configured.");

        var decompressed = compressionCodec.Decompress(payload, maxDecompressedPayloadSize);
        if (decompressed.Length > maxDecompressedPayloadSize)
            throw new InvalidOperationException("Decoded stream payload exceeds maximum stream payload size.");

        return decompressed;
    }

    private sealed class DefaultPacketNameResolver : IZlinkStreamPacketNameResolver
    {
        private static readonly ConcurrentDictionary<Type, string> Cache = new();

        public string Resolve(Type payloadType)
        {
            ArgumentNullException.ThrowIfNull(payloadType);
            return Cache.GetOrAdd(payloadType, static type =>
                type.GetCustomAttribute<ZlinkStreamPacketNameAttribute>(false)?.Name
                ?? type.Name);
        }
    }

    private sealed class Lz4CompressionCodec : IZlinkStreamCompressionCodec
    {
        public ReadOnlyMemory<byte> Compress(ReadOnlyMemory<byte> payload)
        {
            return Lz4Compress(payload);
        }

        public ReadOnlyMemory<byte> Decompress(ReadOnlyMemory<byte> payload, int maxDecompressedPayloadSize)
        {
            return Lz4Decompress(payload, maxDecompressedPayloadSize);
        }
    }
}