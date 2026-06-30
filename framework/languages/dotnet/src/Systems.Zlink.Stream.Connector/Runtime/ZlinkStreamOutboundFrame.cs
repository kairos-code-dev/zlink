namespace Systems.Zlink.Stream.Connector.Runtime;

internal readonly record struct ZlinkStreamOutboundFrame(
    ZlinkStreamHeader Header,
    ReadOnlyMemory<byte> HeaderBytes,
    ReadOnlyMemory<byte> PayloadBytes);