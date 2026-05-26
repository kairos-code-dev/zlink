namespace Zlink.Framework.Contracts.Channels;

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
