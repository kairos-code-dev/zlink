namespace Systems.Zlink.Stream.Connector.Contracts;

public interface IZlinkStreamConnector : IAsyncDisposable
{
    event Func<ZlinkStreamError, CancellationToken, ValueTask>? ErrorReceived;

    event Func<CancellationToken, ValueTask>? Disconnected;

    bool IsConnected { get; }

    ZlinkStreamConnectorOptions Options { get; }

    ValueTask ConnectAsync(CancellationToken cancellationToken = default);

    ValueTask CloseAsync(CancellationToken cancellationToken = default);

    IZlinkStreamSendCall Send(ZlinkStreamEncodedBody body);

    IZlinkStreamRequestCall Request(ZlinkStreamEncodedBody body);

    IDisposable On(
        string name,
        Func<ZlinkStreamMessage<ZlinkStreamEncodedBody>, CancellationToken, ValueTask> handler);
}
