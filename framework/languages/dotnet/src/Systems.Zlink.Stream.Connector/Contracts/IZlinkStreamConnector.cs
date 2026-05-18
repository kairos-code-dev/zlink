namespace Systems.Zlink.Stream.Connector.Contracts;

public interface IZlinkStreamConnector : IAsyncDisposable
{
    event Func<ZlinkStreamError, CancellationToken, ValueTask>? ErrorReceived;

    event Func<CancellationToken, ValueTask>? Disconnected;

    event Func<ZlinkStreamConnectionStateChanged, CancellationToken, ValueTask>? ConnectionStateChanged;

    bool IsConnected { get; }

    ZlinkStreamConnectionState State { get; }

    ZlinkStreamConnectorOptions Options { get; }

    ValueTask ConnectAsync(CancellationToken cancellationToken = default);

    ValueTask CloseAsync(CancellationToken cancellationToken = default);

    IZlinkStreamSendCall Send(ZlinkStreamEncodedPayload payload);

    IZlinkStreamRequestCall Request(ZlinkStreamEncodedPayload payload);

    IDisposable On(
        string name,
        Func<ZlinkStreamMessage<ZlinkStreamEncodedPayload>, CancellationToken, ValueTask> handler);
}
