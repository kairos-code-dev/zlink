namespace Systems.Zlink.Stream.Connector.Runtime;

internal interface IZlinkStreamConnectorInternal : IZlinkStreamConnector
{
    ValueTask SendEncodedAsync(
        ZlinkStreamMessageKind kind,
        string name,
        ZlinkStreamEncodedPayload payload,
        ZlinkStreamMetadata metadata,
        bool compress,
        CancellationToken cancellationToken);

    ValueTask<ZlinkStreamEncodedPayload> RequestEncodedAsync(
        string name,
        ZlinkStreamEncodedPayload payload,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        CancellationToken cancellationToken);

    void RequestEncoded(
        string name,
        ZlinkStreamEncodedPayload payload,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        Action<ZlinkStreamResult> callback);

    void RequestEncoded(
        string name,
        ZlinkStreamEncodedPayload payload,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        Action<ZlinkStreamResult<ZlinkStreamEncodedPayload>> callback);
}
