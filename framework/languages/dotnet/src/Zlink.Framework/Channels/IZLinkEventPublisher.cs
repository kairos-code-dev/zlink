namespace Zlink.Framework.Channels;

public interface IZLinkEventPublisher
{
    IZLinkPublishCall Publish<TEvent>(
        string channelName,
        string topic,
        TEvent message);
}

public interface IZLinkChannelConnectionManager
{
    ValueTask<IZLinkEndpointConnections> GetClientAsync(
        string channelName,
        CancellationToken cancellationToken = default);

    ValueTask<IZLinkEndpointConnections> GetSubscriberAsync(
        string channelName,
        CancellationToken cancellationToken = default);
}
