namespace Systems.Zlink.Stream.Connector.Contracts;

public interface IZlinkStreamHeaderCodec
{
    ReadOnlyMemory<byte> Encode(ZlinkStreamHeader header);

    ZlinkStreamHeader Decode(ReadOnlyMemory<byte> header);
}

public interface IZlinkStreamPacketNameResolver
{
    string Resolve(Type bodyType);
}

public interface IZlinkStreamCompressionCodec
{
    ZlinkStreamCompression Compression { get; }

    ReadOnlyMemory<byte> Compress(ReadOnlyMemory<byte> payload);

    ReadOnlyMemory<byte> Decompress(ReadOnlyMemory<byte> payload);
}
