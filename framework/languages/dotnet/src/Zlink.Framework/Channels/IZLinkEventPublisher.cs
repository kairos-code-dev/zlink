namespace Zlink.Framework;

public interface IZLinkEventPublisher
{
    IZLinkPublishCall Publish<TEvent>(
        string channelName,
        string topic,
        TEvent message);
}

public interface IZLinkChannelConnectionManager
{
    IChannelClientConnections GetClient(string channelName);

    IChannelSubscriberConnections GetSubscriber(string channelName);
}
