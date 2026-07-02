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

    void ValidateSendEncoded(
        ZlinkStreamMessageKind kind,
        string name,
        ZlinkStreamEncodedPayload payload,
        ZlinkStreamMetadata metadata,
        bool compress);

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

    ValueTask<ZlinkStreamMessage<ZlinkStreamEncodedPayload>> WaitForEncodedAsync(
        string name,
        Func<ZlinkStreamMessage<ZlinkStreamEncodedPayload>, bool>? predicate,
        TimeSpan timeout,
        CancellationToken cancellationToken);
}
