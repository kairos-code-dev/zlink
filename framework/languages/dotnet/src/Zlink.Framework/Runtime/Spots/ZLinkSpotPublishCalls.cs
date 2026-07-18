using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkCurrentSpotPublishCall<TEvent>(
    IZLinkCurrentSpotActivation activation,
    string topic,
    TEvent message) : IZLinkPublishCall
{
    private readonly ZLinkCallMetadata _metadata = new();
    private readonly string _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public IZLinkPublishCall Metadata(string key, string value)
    {
        _metadata.Set(key, value);
        return this;
    }

    public IZLinkPublishCall Metadata(ZLinkMessageMetadata metadata)
    {
        _metadata.Merge(metadata);
        return this;
    }

    public ZLinkPublishResult TrySubmit()
    {
        throw ZLinkMeshCallSupport.TrySubmitPendingSyncAdmission();
    }

    public async ValueTask<ZLinkPublishResult> SubmitAsync(
        CancellationToken cancellationToken = default)
    {
        using var flow = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            activation.Flow.CaptureEnabled);
        cancellationToken.ThrowIfCancellationRequested();
        var parts = ZLinkSpotPublishEnvelope.EncodeParts(
            activation.ChannelName,
            _messageName,
            topic,
            message,
            activation.Codecs);
        if (activation.Flow.Enabled(ZLinkMessageFlowOutcome.Sent))
            activation.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.Sent,
                ZLinkDispatchErrorSurface.SpotSubscription,
                ZLinkDispatchMessageKind.Publish,
                _messageName,
                activation.ChannelName,
                topic,
                SpotRid: activation.SpotRid.ToString()));
        MeshPublishDetail detail;
        try
        {
            detail = await activation.OutboundEndpoint
                .PublishCurrentAsync(topic, parts, cancellationToken, _metadata.Encode())
                .ConfigureAwait(false);
        }
        catch (TimeoutException)
        {
            return new ZLinkPublishResult(ZLinkSubmitStatus.TimedOut, default);
        }

        ZLinkRuntimeMetrics.RecordFanoutPublished(null);
        return detail.ToPublishResult();
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
    private readonly ZLinkCallMetadata _metadata = new();
    private readonly string _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public IZLinkPublishCall Metadata(string key, string value)
    {
        _metadata.Set(key, value);
        return this;
    }

    public IZLinkPublishCall Metadata(ZLinkMessageMetadata metadata)
    {
        _metadata.Merge(metadata);
        return this;
    }

    public ZLinkPublishResult TrySubmit()
    {
        throw ZLinkMeshCallSupport.TrySubmitPendingSyncAdmission();
    }

    public async ValueTask<ZLinkPublishResult> SubmitAsync(
        CancellationToken cancellationToken = default)
    {
        using var operation = runtime.EnterOperation();
        using var flow = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            runtime.Flow.CaptureEnabled);
        cancellationToken.ThrowIfCancellationRequested();
        var bundle = runtime.GetSpotPublisherBundle(channelName);
        var packetName = _messageName;
        var parts = ZLinkSpotPublishEnvelope.EncodeParts(
            channelName,
            packetName,
            topic,
            message,
            runtime.Registration.Codecs);

        if (runtime.Flow.Enabled(ZLinkMessageFlowOutcome.Sent))
            runtime.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.Sent,
                ZLinkDispatchErrorSurface.SpotSubscription,
                ZLinkDispatchMessageKind.Publish,
                packetName,
                channelName,
                topic,
                SpotRid: bundle.Spot.RoutingId.ToString()));

        var metadata = _metadata.Encode();
        MeshPublishDetail? detail = null;
        try
        {
            await (bundle.Submitter
                    ?? throw new InvalidOperationException(
                        "External SPOT publish submitter is not initialized."))
                .Async(
                    parts,
                    pending => TryPublish(bundle.Spot, pending, metadata, out detail),
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (TimeoutException)
        {
            return new ZLinkPublishResult(ZLinkSubmitStatus.TimedOut, default);
        }

        ZLinkRuntimeMetrics.RecordFanoutPublished(null);
        return (detail
                ?? throw new InvalidOperationException(
                    "Publish completed without a fan-out detail."))
            .ToPublishResult();
    }

    private bool TryPublish(
        IZLinkBackendSpot spot,
        IReadOnlyList<Message> parts,
        ReadOnlyMemory<byte> metadata,
        out MeshPublishDetail? detail)
    {
        try
        {
            detail = spot.Publish(topic, parts, SendFlags.DontWait, metadata);
            return true;
        }
        catch (ZlinkSubmitException ex) when
            (ex.Result == ZlinkSubmitException.ErrorCode.Backpressured)
        {
            detail = null;
            return false;
        }
    }
}
internal static class ZLinkPublishResultMapper
{
    public static ZLinkPublishResult ToPublishResult(this MeshPublishDetail detail)
    {
        return new ZLinkPublishResult(
            ZLinkSubmitStatus.Submitted,
            new ZLinkLogicalMulticastDetail(
                detail.SnapshotRemoteTargets,
                detail.AdmittedRemoteTargets,
                detail.DroppedRemoteTargets,
                detail.SnapshotLocalSpots,
                detail.AdmittedLocalSpots,
                detail.DroppedLocalSpots));
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
