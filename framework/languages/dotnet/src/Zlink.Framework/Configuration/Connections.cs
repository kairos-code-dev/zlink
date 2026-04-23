namespace Zlink.Framework;

public interface IChannelClientConnections
{
    void Connect(string endpoint);

    void Disconnect(string endpoint);

    IReadOnlyList<string> ListConnections();
}

public interface IChannelSubscriberConnections
{
    void Connect(string endpoint);

    void Disconnect(string endpoint);

    IReadOnlyList<string> ListConnections();
}

public interface ISpotRouterConnections
{
    void Connect(string endpoint);

    void Disconnect(string endpoint);

    IReadOnlyList<string> ListConnections();
}

public interface ISpotPubSubConnections
{
    void Connect(string endpoint);

    void Disconnect(string endpoint);

    IReadOnlyList<string> ListConnections();
}

public interface ISpotPublisherConnections
{
    void Connect(string endpoint);

    void Disconnect(string endpoint);

    IReadOnlyList<string> ListConnections();
}

public interface IZLinkEndpointConnections
{
    ValueTask<bool> ConnectAsync(
        string endpoint,
        CancellationToken cancellationToken = default);

    ValueTask DisconnectAsync(
        string endpoint,
        CancellationToken cancellationToken = default);

    ValueTask<IReadOnlyList<string>> ListConnectionsAsync(
        CancellationToken cancellationToken = default);
}
