using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkCurrentSpotPublishCall<TEvent>(
    ZLinkSpotActivation activation,
    string topic,
    TEvent message) : IZLinkPublishCall
{
    private string? _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public IZLinkPublishCall WithPacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public ValueTask Async(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
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
        if (!activation.PublishCurrent(topic, envelope, SendFlags.None))
        {
            throw new InvalidOperationException("SPOT publish submit failed.");
        }

        return ValueTask.CompletedTask;
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

    public IZLinkPublishCall WithPacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public ValueTask Async(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
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
        if (!bundle.Spot.Publish(channelName, topic, envelope, SendFlags.None))
        {
            throw new InvalidOperationException("External SPOT publish submit failed.");
        }

        return ValueTask.CompletedTask;
    }
}
