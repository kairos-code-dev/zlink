namespace Systems.Zlink.Stream.Connector.Contracts;

public interface IZlinkStreamConnector : IAsyncDisposable
{
    event Func<ZlinkStreamError, CancellationToken, ValueTask>? ErrorReceived;

    event Func<CancellationToken, ValueTask>? Disconnected;

    bool IsConnected { get; }

    ZlinkStreamConnectorOptions Options { get; }

    ValueTask ConnectAsync(CancellationToken cancellationToken = default);

    ValueTask CloseAsync(CancellationToken cancellationToken = default);

    IZlinkStreamSendCall Send(ZlinkStreamEncodedPayload payload);

    IZlinkStreamRequestCall Request(ZlinkStreamEncodedPayload payload);

    IDisposable On(
        string name,
        Func<ZlinkStreamMessage<ZlinkStreamEncodedPayload>, CancellationToken, ValueTask> handler);
}
