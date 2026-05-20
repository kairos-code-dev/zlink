namespace Systems.Zlink.Stream.Connector.Runtime.Protocol;

internal static class ZlinkStreamDefaultCodecFactory
{
    internal static ZlinkStreamHeaderCodec Header()
        => new ZlinkStreamHeaderCodec();

    public static IZlinkStreamPacketNameResolver PacketNameResolver()
        => new ZlinkStreamPacketNameResolver();

    public static IZlinkStreamCompressionCodec Lz4Compression()
        => new ZlinkStreamLz4CompressionCodec();
}
