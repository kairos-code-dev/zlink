using K4os.Compression.LZ4;

namespace Systems.Zlink.Stream.Connector.Runtime.Protocol.Compression;

internal sealed class ZlinkStreamLz4CompressionCodec : IZlinkStreamCompressionCodec
{
    public ZlinkStreamCompression Compression => ZlinkStreamCompression.Lz4;

    public ReadOnlyMemory<byte> Compress(ReadOnlyMemory<byte> body)
        => LZ4Pickler.Pickle(body.Span);

    public ReadOnlyMemory<byte> Decompress(ReadOnlyMemory<byte> body)
        => LZ4Pickler.Unpickle(body.Span);
}
