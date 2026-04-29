namespace Systems.Zlink.Stream.Connector.Runtime;

internal sealed record ZlinkStreamHandlerWorkItem(
    ZlinkStreamHeader Header,
    ReadOnlyMemory<byte> Payload,
    IReadOnlyList<ZlinkStreamTypedHandlerRegistry.TypedHandler> Handlers);
