using Zlink.Framework.Runtime.Messaging;

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

    public void Submit(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var parts = ZLinkSpotPublishEnvelope.EncodeParts(
            activation.ChannelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            topic,
            message,
            activation.Codecs);
        ZLinkUnawaitedSubmit.Observe(
            activation.OutboundEndpoint.PublishCurrentAsync(topic, parts, cancellationToken),
            "spot publish submit");
    }
}

internal sealed class ZLinkSpotPublisherClientService(ZLinkFrameworkRuntime runtime)
    : IZLinkSpotPublisherClient
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

    public IZLinkPublishCall PacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public void Submit(CancellationToken cancellationToken = default)
    {
        ZLinkUnawaitedSubmit.Observe(SubmitAsync(cancellationToken), "spot publish submit");
    }

    private ValueTask SubmitAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var bundle = runtime.GetSpotPublisherBundle(channelName);
        var packetName = _messageName ?? throw new InvalidOperationException("Message name is required.");
        var correlationId = Guid.NewGuid().ToString("N");
        var parts = ZLinkSpotPublishEnvelope.EncodeParts(
            channelName,
            packetName,
            topic,
            message,
            runtime.Registration.Codecs,
            correlationId);

        if (runtime.Flow.Enabled(ZLinkMessageFlowOutcome.Sent))
            runtime.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.Sent,
                ZLinkDispatchErrorSurface.SpotSubscription,
                ZLinkDispatchMessageKind.Publish,
                packetName,
                channelName,
                topic,
                correlationId));

        return (bundle.Submitter
                ?? throw new InvalidOperationException("External SPOT publish submitter is not initialized."))
            .Async(
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
        TEvent message,
        ZLinkCodecRegistryBuilder? codecs = null,
        string? correlationId = null)
    {
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Publish,
            channelName,
            messageName,
            ZLinkEnvelopeCodec.DefaultContentType,
            correlationId,
            null,
            topic,
            null,
            null,
            channelName);
        return ZLinkEnvelopeCodec.EncodeParts(
            header,
            message,
            message?.GetType() ?? typeof(TEvent),
            codecs);
    }
}
