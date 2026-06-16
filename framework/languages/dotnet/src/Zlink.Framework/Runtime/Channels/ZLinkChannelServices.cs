using System.Collections.Concurrent;
using System.Reflection;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkChannelClient(ZLinkFrameworkRuntime runtime, ZLinkFrameworkRegistration registration)
    : IZLinkChannelClient
{
    public IZLinkSendCall SendToChannel<TMessage>(string channelName, TMessage message)
    {
        return new ZLinkSendCall(runtime, registration, channelName, message);
    }

    public IZLinkRequestCall RequestToChannel<TMessage>(string channelName, TMessage request)
        => new ZLinkRequestCall<TMessage>(runtime, registration, channelName, request);
}

internal sealed class ZLinkFanoutClient(ZLinkFrameworkRuntime runtime, ZLinkFrameworkRegistration registration)
    : IZLinkFanoutClient
{
    public IZLinkPublishCall Publish<TEvent>(string channelName, string topic, TEvent message)
    {
        return new ZLinkPublishCall(runtime, registration, channelName, topic, message);
    }
}
