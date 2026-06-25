using K4os.Compression.LZ4;

namespace Systems.Zlink.Stream.Connector.Runtime.Protocol.Compression;

internal sealed class ZlinkStreamLz4CompressionCodec : IZlinkStreamCompressionCodec
{
    public ReadOnlyMemory<byte> Compress(ReadOnlyMemory<byte> payload)
        => LZ4Pickler.Pickle(payload.Span);

    public ReadOnlyMemory<byte> Decompress(ReadOnlyMemory<byte> payload, int maxDecompressedPayloadSize)
    {
        var decompressedSize = LZ4Pickler.UnpickledSize(payload.Span);
        if (decompressedSize > maxDecompressedPayloadSize)
        {
            throw ZlinkStreamConnector.Error(
                ZlinkStreamErrorCode.FrameTooLarge,
                "LZ4 decoded payload exceeds MaxReceivePayloadSize.");
        }

        return LZ4Pickler.Unpickle(payload.Span);
    }
}
