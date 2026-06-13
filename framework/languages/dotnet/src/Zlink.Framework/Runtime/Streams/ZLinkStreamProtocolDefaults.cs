using System.Collections.Concurrent;
using System.Reflection;
using K4os.Compression.LZ4;

namespace Zlink.Framework.Runtime.Streams;

internal static class ZLinkStreamProtocolDefaults
{
    private const int DefaultMaxDecompressedPayloadSize = 64 * 1024;

    public static IZlinkStreamPacketNameResolver PacketNameResolver { get; } = new DefaultPacketNameResolver();

    public static ReadOnlyMemory<byte> EncodeHeader(ZlinkStreamHeader header)
        => ZLinkStreamHeaderCodec.Encode(header);

    public static ZlinkStreamHeader DecodeHeader(ReadOnlyMemory<byte> header)
        => ZLinkStreamHeaderCodec.Decode(header);

    public static ReadOnlyMemory<byte> Lz4Compress(ReadOnlyMemory<byte> payload)
        => LZ4Pickler.Pickle(payload.Span);

    public static ReadOnlyMemory<byte> Lz4Decompress(ReadOnlyMemory<byte> payload)
        => Lz4Decompress(payload, DefaultMaxDecompressedPayloadSize);

    public static ReadOnlyMemory<byte> Lz4Decompress(
        ReadOnlyMemory<byte> payload,
        int maxDecompressedPayloadSize)
    {
        var decompressedSize = LZ4Pickler.UnpickledSize(payload.Span);
        if (decompressedSize > maxDecompressedPayloadSize)
        {
            throw new InvalidOperationException("LZ4 decoded stream payload exceeds maximum stream payload size.");
        }

        return LZ4Pickler.Unpickle(payload.Span);
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
}
