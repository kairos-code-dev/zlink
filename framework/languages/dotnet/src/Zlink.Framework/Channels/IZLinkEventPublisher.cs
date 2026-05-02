namespace Zlink.Framework.Channels;

public interface IZLinkFanoutPublisher
{
    IZLinkPublishCall Publish<TEvent>(
        string channelName,
        string topic,
        TEvent message);
}

public interface IZLinkEventPublisher : IZLinkFanoutPublisher
{
}

public interface IZLinkChannelConnectionManager
{
    ValueTask<IZLinkEndpointConnections> GetClientServerClientAsync(
        string channelName,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkEndpointConnections> GetFanoutSubscriberAsync(
        string channelName,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkEndpointConnections> GetClientAsync(
        string channelName,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkEndpointConnections> GetSubscriberAsync(
        string channelName,
        CancellationToken cancellationToken = default);
}
