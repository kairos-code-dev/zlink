using K4os.Compression.LZ4;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkLz4StreamCompressionCodec : IZlinkStreamCompressionCodec
{
    public static ReadOnlyMemory<byte> CompressPayload(ReadOnlyMemory<byte> payload)
    {
        return LZ4Pickler.Pickle(payload.Span);
    }

    public static ReadOnlyMemory<byte> DecompressPayload(
        ReadOnlyMemory<byte> payload,
        int maxDecompressedPayloadSize)
    {
        var decompressedSize = LZ4Pickler.UnpickledSize(payload.Span);
        if (decompressedSize > maxDecompressedPayloadSize)
            throw new InvalidOperationException("LZ4 decoded stream payload exceeds maximum stream payload size.");

        return LZ4Pickler.Unpickle(payload.Span);
    }

    public ReadOnlyMemory<byte> Compress(ReadOnlyMemory<byte> payload)
    {
        return CompressPayload(payload);
    }

    public ReadOnlyMemory<byte> Decompress(ReadOnlyMemory<byte> payload, int maxDecompressedPayloadSize)
    {
        return DecompressPayload(payload, maxDecompressedPayloadSize);
    }
}
