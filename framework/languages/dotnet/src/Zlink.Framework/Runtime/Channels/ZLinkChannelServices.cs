using System.Collections.Concurrent;
using System.Reflection;
using System.Text.Json;
using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkClient(ZLinkFrameworkRuntime runtime, ZLinkFrameworkRegistration registration) : IZLinkClient
{
    public IZLinkSendCall Send<TMessage>(string channelName, TMessage message)
    {
        return new ZLinkSendCall(runtime, registration, channelName, message);
    }

    public IZLinkRequestCall Request<TMessage>(string channelName, TMessage request)
        => new ZLinkRequestCall<TMessage>(runtime, registration, channelName, request);
}

internal sealed class ZLinkEventPublisher(ZLinkFrameworkRuntime runtime) : IZLinkEventPublisher
{
    public IZLinkPublishCall Publish<TEvent>(string channelName, string topic, TEvent message)
    {
        return new ZLinkPublishCall(runtime, channelName, topic, message);
    }
}
