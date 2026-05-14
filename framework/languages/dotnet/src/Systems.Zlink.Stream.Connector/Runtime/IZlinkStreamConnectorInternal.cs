namespace Systems.Zlink.Stream.Connector.Runtime;

internal interface IZlinkStreamConnectorInternal : IZlinkStreamConnector
{
    ValueTask SendEncodedAsync(
        ZlinkStreamMessageKind kind,
        string name,
        ZlinkStreamEncodedBody body,
        ZlinkStreamMetadata metadata,
        bool compress,
        CancellationToken cancellationToken);

    ValueTask<ZlinkStreamEncodedBody> RequestEncodedAsync(
        string name,
        ZlinkStreamEncodedBody body,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        CancellationToken cancellationToken);

    void RequestEncoded(
        string name,
        ZlinkStreamEncodedBody body,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        Action<ZlinkStreamResult> callback);

    void RequestEncoded(
        string name,
        ZlinkStreamEncodedBody body,
        ZlinkStreamMetadata metadata,
        bool compress,
        TimeSpan timeout,
        Action<ZlinkStreamResult<ZlinkStreamEncodedBody>> callback);
}
