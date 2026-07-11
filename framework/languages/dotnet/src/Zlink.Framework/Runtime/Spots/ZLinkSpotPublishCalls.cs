using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkCurrentSpotPublishCall<TEvent>(
    IZLinkCurrentSpotActivation activation,
    string topic,
    TEvent message) : IZLinkPublishCall
{
    private readonly string _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public void Submit(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var parts = ZLinkSpotPublishEnvelope.EncodeParts(
            activation.ChannelName,
            _messageName,
            topic,
            message,
            activation.Codecs);
        ZLinkUnawaitedSubmit.Observe(
            SubmitAsync(parts, cancellationToken),
            "spot publish submit");
    }

    private async ValueTask SubmitAsync(
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        await activation.OutboundEndpoint.PublishCurrentAsync(topic, parts, cancellationToken)
            .ConfigureAwait(false);
        ZLinkRuntimeMetrics.RecordFanoutPublished(null);
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
    private readonly string _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public void Submit(CancellationToken cancellationToken = default)
    {
        using (var operation = runtime.EnterOperation())
            runtime.GetSpotPublisherBundle(channelName);
        ZLinkUnawaitedSubmit.Observe(SubmitAsync(cancellationToken), "spot publish submit");
    }

    private async ValueTask SubmitAsync(CancellationToken cancellationToken)
    {
        using var operation = runtime.EnterOperation();
        using var flow = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            runtime.Flow.GenerationEnabled);
        cancellationToken.ThrowIfCancellationRequested();
        var bundle = runtime.GetSpotPublisherBundle(channelName);
        var packetName = _messageName;
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

        await (bundle.Submitter
                ?? throw new InvalidOperationException("External SPOT publish submitter is not initialized."))
            .Async(
                parts,
                pending => bundle.Spot.Publish(topic, pending, SendFlags.DontWait),
                cancellationToken).ConfigureAwait(false);
        ZLinkRuntimeMetrics.RecordFanoutPublished(null);
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
