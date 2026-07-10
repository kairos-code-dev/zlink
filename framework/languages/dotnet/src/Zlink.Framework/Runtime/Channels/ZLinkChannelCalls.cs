using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkSendCall : IZLinkSendCall
{
    private readonly string _channelName;
    private readonly object? _message;
    private readonly ZLinkFrameworkRegistration _registration;
    private readonly ZLinkFrameworkRuntime _runtime;
    private string? _messageName;

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

    public IZLinkSendCall PacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public void Submit(CancellationToken cancellationToken = default)
    {
        ZLinkUnawaitedSubmit.Observe(SubmitAsync(cancellationToken), "channel submit");
    }

    private ValueTask SubmitAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var bundle = _runtime.GetClientBundle(_channelName);
        var dealer = (IZLinkBackendDealerSocket)bundle.Socket;
        var traceSent = _runtime.Flow.Enabled(ZLinkMessageFlowOutcome.Sent);
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            _channelName,
            _messageName ?? ZLinkMessageNameResolver.ResolveFromMessage(_message),
            includeCorrelationId: traceSent,
            includeDeadline: false);

        var message = ZLinkEnvelopeCodec.EncodeParts(header, _message, _message?.GetType(), _registration.Codecs);

        if (traceSent)
            _runtime.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.Sent,
                ZLinkDispatchErrorSurface.Channel,
                ZLinkDispatchMessageKind.Send,
                header.MessageName,
                _channelName,
                CorrelationId: header.CorrelationId,
                LocalRid: bundle.LocalRid,
                SocketRole: bundle.SocketRole));

        return (bundle.Submitter
                ?? throw new InvalidOperationException("ZLink send submitter is not initialized."))
            .Async(
                message,
                pending => dealer.Send(pending, SendFlags.DontWait),
                cancellationToken);
    }
}

internal sealed class ZLinkRequestCall<TMessage>(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration,
    string channelName,
    TMessage request)
    : IZLinkYieldRequestCall
{
    private readonly ZLinkSerialTurn? _turn = ZLinkSerialTurn.Current;
    private string? _messageName;
    private TimeSpan? _timeout;

    public IZLinkYieldRequestCall PacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public IZLinkYieldRequestCall Timeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    IZLinkRequestCall IZLinkRequestCall.PacketName(string messageName)
    {
        return PacketName(messageName);
    }

    IZLinkRequestCall IZLinkRequestCall.Timeout(TimeSpan timeout)
    {
        return Timeout(timeout);
    }

    public ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
    {
        var bundle = runtime.GetClientBundle(channelName);
        var dealer = (IZLinkBackendDealerSocket)bundle.Socket;
        var timeout = _timeout ?? registration.ResolveChannelRequestTimeout(channelName);
        var traceSent = runtime.Flow.Enabled(ZLinkMessageFlowOutcome.Sent);
        var traceReply = runtime.Flow.Enabled(ZLinkMessageFlowOutcome.ReplyReceived);
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            channelName,
            _messageName ?? ZLinkMessageNameResolver.ResolveFromMessage(request),
            timeout,
            includeCorrelationId: traceSent || traceReply,
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

        if (!traceReply) return reply;

        return TraceReplyAsync(reply, header, bundle.LocalRid, bundle.SocketRole);
    }

    private async ValueTask<TReply> TraceReplyAsync<TReply>(
        ValueTask<TReply> reply,
        ZLinkEnvelopeHeader header,
        string? localRid,
        string? socketRole)
    {
        var result = await reply.ConfigureAwait(false);

        runtime.Flow.Trace(new ZLinkMessageFlowEvent(
            ZLinkMessageFlowOutcome.ReplyReceived,
            ZLinkDispatchErrorSurface.Channel,
            ZLinkDispatchMessageKind.Response,
            header.MessageName,
            channelName,
            CorrelationId: header.CorrelationId,
            LocalRid: localRid,
            SocketRole: socketRole));

        return result;
    }

    public ValueTask<TReply> Yield<TReply>(CancellationToken cancellationToken = default)
    {
        return RequireTurn().YieldFrameworkCallAsync(Async<TReply>, cancellationToken);
    }

    private ZLinkSerialTurn RequireTurn()
    {
        return _turn
               ?? throw new InvalidOperationException(
                   "Yield requires a framework Spot handler turn captured when the call object was created.");
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
    private string? _messageName;

    public IZLinkPublishCall PacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public void Submit(CancellationToken cancellationToken = default)
    {
        ZLinkUnawaitedSubmit.Observe(SubmitAsync(cancellationToken), "channel submit");
    }

    private ValueTask SubmitAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var bundle = runtime.GetPublisherBundle(channelName);
        var publisher = (IZLinkBackendPublisherSocket)bundle.Socket;
        var traceSent = runtime.Flow.Enabled(ZLinkMessageFlowOutcome.Sent);
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Publish,
            channelName,
            _messageName ?? ZLinkMessageNameResolver.ResolveFromMessage(message),
            topic: topic,
            source: channelName,
            includeCorrelationId: traceSent,
            includeDeadline: false);
        var envelopedMsg = ZLinkEnvelopeCodec.EncodeParts(header, message, message?.GetType(), registration.Codecs);

        if (traceSent)
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

        return (bundle.Submitter
                ?? throw new InvalidOperationException("ZLink publish submitter is not initialized."))
            .Async(
                envelopedMsg,
                pending => publisher.Publish(topic, pending, SendFlags.DontWait),
                cancellationToken);
    }
}
