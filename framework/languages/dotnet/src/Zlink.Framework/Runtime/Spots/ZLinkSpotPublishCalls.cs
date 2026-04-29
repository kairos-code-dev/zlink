using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkCurrentSpotPublishCall<TEvent>(
    ZLinkSpotActivation activation,
    string topic,
    TEvent message) : IZLinkPublishCall
{
    private string? _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);
    private SendFlags _flags;

    public IZLinkPublishCall WithMessageName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public IZLinkPublishCall WithDontWait()
    {
        _flags |= SendFlags.DontWait;
        return this;
    }

    public bool Sync()
    {
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Event,
            activation.ChannelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            null,
            null,
            topic,
            null,
            null);
        using var envelope = ZLinkEnvelopeCodec.Encode(header, message, message?.GetType() ?? typeof(TEvent));
        return activation.PublishCurrent(topic, envelope, _flags);
    }
}

internal sealed class ZLinkSpotPublisherClientService(ZLinkFrameworkRuntime runtime) : IZLinkSpotPublisherClient
{
    public IZLinkPublishCall Publish<TEvent>(string channelName, string topic, TEvent message)
    {
        return new ZLinkExternalSpotPublishCall<TEvent>(runtime, channelName, topic, message);
    }
}

internal sealed class ZLinkExternalSpotPublishCall<TEvent>(
    ZLinkFrameworkRuntime runtime,
    string channelName,
    string topic,
    TEvent message) : IZLinkPublishCall
{
    private string? _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);
    private SendFlags _flags;

    public IZLinkPublishCall WithMessageName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public IZLinkPublishCall WithDontWait()
    {
        _flags |= SendFlags.DontWait;
        return this;
    }

    public bool Sync()
    {
        var bundle = runtime.GetSpotPublisherBundle(channelName);
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Event,
            channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            null,
            null,
            topic,
            null,
            null);
        using var envelope = ZLinkEnvelopeCodec.Encode(header, message, message?.GetType() ?? typeof(TEvent));
        return bundle.Spot.Publish(channelName, topic, envelope, _flags);
    }
}
