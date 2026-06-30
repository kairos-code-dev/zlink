namespace Zlink.Framework.Contracts.Channels;

public interface IZLinkFanoutClient
{
    IZLinkPublishCall Publish<TEvent>(
        string channelName,
        string topic,
        TEvent message);
}