namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelClient(ZLinkFrameworkRuntime runtime, ZLinkFrameworkRegistration registration)
    : IZLinkChannelClient
{
    public IZLinkSendCall SendToChannel<TMessage>(string channelName, TMessage message)
    {
        return new ZLinkSendCall(runtime, registration, channelName, message);
    }

    public IZLinkYieldRequestCall RequestToChannel<TMessage>(string channelName, TMessage request)
    {
        return new ZLinkRequestCall<TMessage>(runtime, registration, channelName, request);
    }
}

internal sealed class ZLinkFanoutClient(ZLinkFrameworkRuntime runtime, ZLinkFrameworkRegistration registration)
    : IZLinkFanoutClient
{
    public IZLinkPublishCall Publish<TEvent>(string channelName, string topic, TEvent message)
    {
        return new ZLinkPublishCall(runtime, registration, channelName, topic, message);
    }
}
