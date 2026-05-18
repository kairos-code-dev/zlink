using K4os.Compression.LZ4;

namespace Systems.Zlink.Stream.Connector.Runtime.Protocol.Compression;

internal sealed class ZlinkStreamLz4CompressionCodec : IZlinkStreamCompressionCodec
{
    public ZlinkStreamCompression Compression => ZlinkStreamCompression.Lz4;

    public ReadOnlyMemory<byte> Compress(ReadOnlyMemory<byte> payload)
        => LZ4Pickler.Pickle(payload.Span);

    public ReadOnlyMemory<byte> Decompress(ReadOnlyMemory<byte> payload)
        => LZ4Pickler.Unpickle(payload.Span);
}
