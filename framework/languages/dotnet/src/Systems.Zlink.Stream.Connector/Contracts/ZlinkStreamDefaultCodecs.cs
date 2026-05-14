namespace Systems.Zlink.Stream.Connector.Contracts;

public static class ZlinkStreamDefaultCodecs
{
    public static IZlinkStreamHeaderCodec Header()
        => ZlinkStreamDefaultCodecFactory.Header();

    public static IZlinkStreamPacketNameResolver PacketNameResolver()
        => ZlinkStreamDefaultCodecFactory.PacketNameResolver();

    public static IZlinkStreamCompressionCodec Lz4Compression()
        => ZlinkStreamDefaultCodecFactory.Lz4Compression();
}
