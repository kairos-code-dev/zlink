using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkSendCall : IZLinkSendCall
{
    private readonly string _channelName;
    private readonly object? _message;
    private readonly ZLinkFrameworkRegistration _registration;
    private readonly ZLinkFrameworkRuntime _runtime;

    public ZLinkSendCall(
        ZLinkFrameworkRuntime runtime,
        ZLinkFrameworkRegistration registration,
        string channelName,
        object? message)
    {
        _runtime = runtime;
        _registration = registration;
        _channelName = channelName;
        _message = message;
    }

    public IZLinkSendCall Metadata(string key, string value)
    {
        throw ZLinkClassicCallSupport.MetadataNotSupported();
    }

    public IZLinkSendCall Metadata(ZLinkMessageMetadata metadata)
    {
        throw ZLinkClassicCallSupport.MetadataNotSupported();
    }

    public ZLinkSubmitResult TrySubmit()
    {
        using var operation = _runtime.EnterOperation();
        using var flow = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            _runtime.Flow.CaptureEnabled);
        var (bundle, dealer, message, header) = Build();
        if (!dealer.Send(message, SendFlags.DontWait))
            return new ZLinkSubmitResult(ZLinkSubmitStatus.Backpressured);
        TraceSent(bundle, header);
        return new ZLinkSubmitResult(ZLinkSubmitStatus.Submitted);
    }

    public async ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default)
    {
        using var operation = _runtime.EnterOperation();
        using var flow = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            _runtime.Flow.CaptureEnabled);
        cancellationToken.ThrowIfCancellationRequested();
        var (bundle, dealer, message, header) = Build();
        try
        {
            await (bundle.Submitter
                    ?? throw new InvalidOperationException(
                        "ZLink send submitter is not initialized."))
                .Async(
                    message,
                    pending => dealer.Send(pending, SendFlags.DontWait),
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (TimeoutException)
        {
            return new ZLinkSubmitResult(ZLinkSubmitStatus.TimedOut);
        }

        TraceSent(bundle, header);
        return new ZLinkSubmitResult(ZLinkSubmitStatus.Submitted);
    }

    private (ZLinkChannelRuntimeBundle Bundle, IZLinkBackendDealerSocket Dealer,
        IReadOnlyList<Message> Message, ZLinkEnvelopeHeader Header) Build()
    {
        var bundle = _runtime.GetClientBundle(_channelName);
        var dealer = (IZLinkBackendDealerSocket)bundle.Socket;
        var traceSent = _runtime.Flow.Enabled(ZLinkMessageFlowOutcome.Sent);
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            _channelName,
            ZLinkMessageNameResolver.ResolveFromMessage(_message),
            includeCorrelationId: traceSent,
            includeDeadline: false);
        var message = ZLinkEnvelopeCodec.EncodeParts(
            header, _message, _message?.GetType(), _registration.Codecs);
        return (bundle, dealer, message, header);
    }

    private void TraceSent(ZLinkChannelRuntimeBundle bundle, ZLinkEnvelopeHeader header)
    {
        if (!_runtime.Flow.Enabled(ZLinkMessageFlowOutcome.Sent))
            return;
        _runtime.Flow.Trace(new ZLinkMessageFlowEvent(
            ZLinkMessageFlowOutcome.Sent,
            ZLinkDispatchErrorSurface.Channel,
            ZLinkDispatchMessageKind.Send,
            header.MessageName,
            _channelName,
            CorrelationId: header.CorrelationId,
            LocalRid: bundle.LocalRid,
            SocketRole: bundle.SocketRole));
    }
}

internal sealed class ZLinkRequestCall<TMessage>(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration,
    string channelName,
    TMessage request)
    : IZLinkRequestCall
{
    private readonly ZLinkSerialTurn? _turn = ZLinkSerialTurn.Current;
    private TimeSpan? _timeout;

    public IZLinkRequestCall Timeout(TimeSpan timeout)
    {
        ZLinkRequestTimeoutValidation.Validate(timeout, nameof(timeout));
        _timeout = timeout;
        return this;
    }

    public IZLinkRequestCall Metadata(string key, string value)
    {
        throw ZLinkClassicCallSupport.MetadataNotSupported();
    }

    public IZLinkRequestCall Metadata(ZLinkMessageMetadata metadata)
    {
        throw ZLinkClassicCallSupport.MetadataNotSupported();
    }

    public ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
    {
        return ExecuteAsync<TReply>(cancellationToken);
    }

    public ValueTask<TReply> Yield<TReply>(CancellationToken cancellationToken = default)
    {
        return _turn is null
            ? ExecuteAsync<TReply>(cancellationToken)
            : _turn.YieldFrameworkCallAsync(ExecuteAsync<TReply>, cancellationToken);
    }

    private async ValueTask<TReply> ExecuteAsync<TReply>(CancellationToken cancellationToken)
    {
        using var operation = runtime.EnterOperation(countAsRequest: true);
        using var flow = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            runtime.Flow.CaptureEnabled);
        var bundle = runtime.GetClientBundle(channelName);
        var dealer = (IZLinkBackendDealerSocket)bundle.Socket;
        var timeout = _timeout ?? registration.ResolveChannelRequestTimeout(channelName);
        var traceSent = runtime.Flow.Enabled(ZLinkMessageFlowOutcome.Sent);
        var traceReply = runtime.Flow.Enabled(ZLinkMessageFlowOutcome.ReplyReceived);
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            channelName,
            ZLinkMessageNameResolver.ResolveFromMessage(request),
            timeout,
            includeDeadline: false);
        var message = ZLinkClientCallCodec.EncodeEnvelopeParts(header, request, registration.Codecs);

        if (traceSent)
            runtime.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.Sent,
                ZLinkDispatchErrorSurface.Channel,
                ZLinkDispatchMessageKind.Request,
                header.MessageName,
                channelName,
                CorrelationId: header.CorrelationId,
                LocalRid: bundle.LocalRid,
                SocketRole: bundle.SocketRole));

        var metricStarted = ZLinkRuntimeMetrics.StartChannelRequest();
        var timedOut = false;
        try
        {
            var reply = (bundle.Submitter
                        ?? throw new InvalidOperationException("ZLink request submitter is not initialized."))
                .SubmitRequestAsync<TReply>(
                    message,
                    (pending, complete, fail) => dealer.Request(
                        pending,
                        (result, replyMessage) => ZLinkEnvelopeReplyCompletion.Complete(
                            result,
                            replyMessage,
                            complete,
                            fail,
                            "ZLink request",
                            registration.Codecs),
                        SendFlags.DontWait,
                        timeout),
                    cancellationToken);
            var result = await reply.ConfigureAwait(false);
            if (traceReply)
                runtime.Flow.Trace(new ZLinkMessageFlowEvent(
                    ZLinkMessageFlowOutcome.ReplyReceived,
                    ZLinkDispatchErrorSurface.Channel,
                    ZLinkDispatchMessageKind.Response,
                    header.MessageName,
                    channelName,
                    CorrelationId: header.CorrelationId,
                    LocalRid: bundle.LocalRid,
                    SocketRole: bundle.SocketRole));
            return result;
        }
        catch (TimeoutException)
        {
            timedOut = true;
            throw;
        }
        finally
        {
            ZLinkRuntimeMetrics.CompleteChannelRequest(metricStarted, timedOut);
        }
    }
}

internal sealed class ZLinkPublishCall(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration,
    string channelName,
    string topic,
    object? message)
    : IZLinkPublishCall
{
    public IZLinkPublishCall Metadata(string key, string value)
    {
        throw ZLinkClassicCallSupport.MetadataNotSupported();
    }

    public IZLinkPublishCall Metadata(ZLinkMessageMetadata metadata)
    {
        throw ZLinkClassicCallSupport.MetadataNotSupported();
    }

    public ZLinkPublishResult TrySubmit()
    {
        using var operation = runtime.EnterOperation();
        using var flow = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            runtime.Flow.CaptureEnabled);
        var (bundle, publisher, envelopedMsg, header) = Build();
        if (!publisher.Publish(topic, envelopedMsg, SendFlags.DontWait))
            return new ZLinkPublishResult(ZLinkSubmitStatus.Backpressured, default);
        TraceSent(bundle, header);
        ZLinkRuntimeMetrics.RecordFanoutPublished(null);
        return new ZLinkPublishResult(ZLinkSubmitStatus.Submitted, default);
    }

    public async ValueTask<ZLinkPublishResult> SubmitAsync(
        CancellationToken cancellationToken = default)
    {
        using var operation = runtime.EnterOperation();
        using var flow = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            runtime.Flow.CaptureEnabled);
        cancellationToken.ThrowIfCancellationRequested();
        var (bundle, publisher, envelopedMsg, header) = Build();
        try
        {
            await (bundle.Submitter
                    ?? throw new InvalidOperationException(
                        "ZLink publish submitter is not initialized."))
                .Async(
                    envelopedMsg,
                    pending => publisher.Publish(topic, pending, SendFlags.DontWait),
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (TimeoutException)
        {
            return new ZLinkPublishResult(ZLinkSubmitStatus.TimedOut, default);
        }

        TraceSent(bundle, header);
        ZLinkRuntimeMetrics.RecordFanoutPublished(null);
        return new ZLinkPublishResult(ZLinkSubmitStatus.Submitted, default);
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
