using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkCurrentSpotPublishCall<TEvent>(
    IZLinkCurrentSpotActivation activation,
    string topic,
    TEvent message) : IZLinkPublishCall
{
    private string? _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public IZLinkPublishCall PacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public ValueTask SubmitAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var parts = ZLinkSpotPublishEnvelope.EncodeParts(
            activation.ChannelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            topic,
            message);
        return activation.PublishCurrentAsync(topic, parts, cancellationToken);
    }
}

internal sealed class ZLinkSpotPublisherClientService(ZLinkFrameworkRuntime runtime)
    : IZLinkSpotPublisherClient
{
    public IZLinkPublishCall PublishSpot<TEvent>(string channelName, string topic, TEvent message)
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

    public IZLinkPublishCall PacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public ValueTask SubmitAsync(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var bundle = runtime.GetSpotPublisherBundle(channelName);
        var parts = ZLinkSpotPublishEnvelope.EncodeParts(
            channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            topic,
            message);
        return (bundle.Submitter
                ?? throw new InvalidOperationException("External SPOT publish submitter is not initialized."))
            .SubmitAsync(
                parts,
                pending => bundle.Spot.Publish(topic, pending, SendFlags.DontWait),
                cancellationToken);
    }
}

internal static class ZLinkSpotPublishEnvelope
{
    public static IReadOnlyList<Message> EncodeParts<TEvent>(
        string channelName,
        string messageName,
        string topic,
        TEvent message)
    {
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Publish,
            channelName,
            messageName,
            ZLinkEnvelopeCodec.DefaultContentType,
            null,
            null,
            topic,
            null,
            null,
            Source: channelName);
        return ZLinkEnvelopeCodec.EncodeParts(
            header,
            message,
            message?.GetType() ?? typeof(TEvent));
    }
}
