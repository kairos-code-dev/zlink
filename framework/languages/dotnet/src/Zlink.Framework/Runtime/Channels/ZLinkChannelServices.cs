namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkFanoutClient(ZLinkFrameworkRuntime runtime, ZLinkFrameworkRegistration registration)
    : IZLinkFanoutClient
{
    public IZLinkFanoutPublishCall Publish<TEvent>(string channelName, string topic, TEvent message)
    {
        return new ZLinkPublishCall(runtime, registration, channelName, topic, message);
    }
}
