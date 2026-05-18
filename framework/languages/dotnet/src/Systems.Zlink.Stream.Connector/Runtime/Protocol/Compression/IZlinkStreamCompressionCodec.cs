namespace Systems.Zlink.Stream.Connector.Runtime.Protocol.Compression;

internal interface IZlinkStreamCompressionCodec
{
    ReadOnlyMemory<byte> Compress(ReadOnlyMemory<byte> payload);

    ReadOnlyMemory<byte> Decompress(ReadOnlyMemory<byte> payload);
}
