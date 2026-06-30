namespace Systems.Zlink.Stream.Connector.Runtime.Protocol;

internal static class ZlinkStreamDefaultCodecFactory
{
    internal static ZlinkStreamHeaderCodec Header()
    {
        return new ZlinkStreamHeaderCodec();
    }

    public static IZlinkStreamPacketNameResolver PacketNameResolver()
    {
        return new ZlinkStreamPacketNameResolver();
    }

    public static IZlinkStreamCompressionCodec Lz4Compression()
    {
        return new ZlinkStreamLz4CompressionCodec();
    }
}