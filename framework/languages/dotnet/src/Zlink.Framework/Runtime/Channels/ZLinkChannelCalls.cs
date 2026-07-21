using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkPublishCall(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration,
    string channelName,
    string topic,
    object? message)
    : IZLinkFanoutPublishCall
{
    public async ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default)
    {
        using var operation = runtime.EnterOperation();
        using var flow = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            runtime.Flow.CaptureEnabled);
        cancellationToken.ThrowIfCancellationRequested();
        var (bundle, publisher, envelopedMsg, header) = Build();
        var result = await (bundle.Submitter
                    ?? throw new InvalidOperationException(
                        "ZLink publish submitter is not initialized."))
                .SubmitAsync(
                    envelopedMsg,
                    pending => publisher.Publish(topic, pending, SendFlags.DontWait),
                    cancellationToken)
                .ConfigureAwait(false);
        if (result.Status == ZLinkSubmitStatus.Submitted)
        {
            TraceSent(bundle, header);
            ZLinkRuntimeMetrics.RecordFanoutPublished(null);
        }
        return result;
    }

    private (ZLinkChannelRuntimeBundle Bundle, IZLinkBackendPublisherSocket Publisher,
        IReadOnlyList<Message> Message, ZLinkEnvelopeHeader Header) Build()
    {
        var bundle = runtime.GetPublisherBundle(channelName);
        var publisher = (IZLinkBackendPublisherSocket)bundle.Socket;
        var traceSent = runtime.Flow.Enabled(ZLinkMessageFlowOutcome.Sent);
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Publish,
            channelName,
            ZLinkMessageNameResolver.ResolveFromMessage(message),
            topic: topic,
            source: channelName,
            includeCorrelationId: traceSent,
            includeDeadline: false);
        var envelopedMsg = ZLinkEnvelopeCodec.EncodeParts(
            header, message, message?.GetType(), registration.Codecs);
        return (bundle, publisher, envelopedMsg, header);
    }

    private void TraceSent(ZLinkChannelRuntimeBundle bundle, ZLinkEnvelopeHeader header)
    {
        if (!runtime.Flow.Enabled(ZLinkMessageFlowOutcome.Sent))
            return;
        runtime.Flow.Trace(new ZLinkMessageFlowEvent(
            ZLinkMessageFlowOutcome.Sent,
            ZLinkDispatchErrorSurface.Channel,
            ZLinkDispatchMessageKind.Publish,
            header.MessageName,
            channelName,
            topic,
            header.CorrelationId,
            LocalRid: bundle.LocalRid,
            SocketRole: bundle.SocketRole));
    }
}
