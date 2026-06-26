using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Execution;

namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkSendCall : IZLinkSendCall
{
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly ZLinkFrameworkRegistration _registration;
    private readonly string _channelName;
    private readonly object? _message;
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
        _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);
    }

    public IZLinkSendCall PacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public ValueTask Async(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var bundle = _runtime.GetOrCreateClientBundle(_channelName);
        var dealer = (IZLinkBackendDealerSocket)bundle.Socket;
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            _channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."));

        var message = ZLinkEnvelopeCodec.EncodeParts(header, _message, _message?.GetType(), _registration.Codecs);

        if (_runtime.Flow.Enabled(ZLinkMessageFlowOutcome.Sent))
        {
            _runtime.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.Sent,
                ZLinkDispatchErrorSurface.Channel,
                ZLinkDispatchMessageKind.Send,
                PacketName: header.MessageName,
                ChannelName: _channelName,
                CorrelationId: header.CorrelationId,
                LocalRid: bundle.LocalRid,
                SocketRole: bundle.SocketRole));
        }

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
    : IZLinkRequestCall
{
    private readonly ZLinkSerialTurn? _turn = ZLinkSerialTurn.Current;
    private string? _messageName = ZLinkMessageNameResolver.ResolveFromMessage(request);
    private TimeSpan? _timeout;

    public IZLinkRequestCall PacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public IZLinkRequestCall Timeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public async ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
    {
        var bundle = runtime.GetOrCreateClientBundle(channelName);
        var dealer = (IZLinkBackendDealerSocket)bundle.Socket;
        var timeout = _timeout ?? registration.ResolveChannelRequestTimeout(channelName);
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            timeout);
        var message = ZLinkClientCallCodec.EncodeEnvelopeParts(header, request, registration.Codecs);

        if (runtime.Flow.Enabled(ZLinkMessageFlowOutcome.Sent))
        {
            runtime.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.Sent,
                ZLinkDispatchErrorSurface.Channel,
                ZLinkDispatchMessageKind.Request,
                PacketName: header.MessageName,
                ChannelName: channelName,
                CorrelationId: header.CorrelationId,
                LocalRid: bundle.LocalRid,
                SocketRole: bundle.SocketRole));
        }

        var reply = await (bundle.Submitter
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
                cancellationToken)
            .ConfigureAwait(false);

        if (runtime.Flow.Enabled(ZLinkMessageFlowOutcome.ReplyReceived))
        {
            runtime.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.ReplyReceived,
                ZLinkDispatchErrorSurface.Channel,
                ZLinkDispatchMessageKind.Response,
                PacketName: header.MessageName,
                ChannelName: channelName,
                CorrelationId: header.CorrelationId,
                LocalRid: bundle.LocalRid,
                SocketRole: bundle.SocketRole));
        }

        return reply;
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
    private string? _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public IZLinkPublishCall PacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public ValueTask Async(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var bundle = runtime.GetOrCreatePublisherBundle(channelName);
        var publisher = (IZLinkBackendPublisherSocket)bundle.Socket;
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Publish,
            channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            topic: topic,
            source: channelName);
        var envelopedMsg = ZLinkEnvelopeCodec.EncodeParts(header, message, message?.GetType(), registration.Codecs);

        if (runtime.Flow.Enabled(ZLinkMessageFlowOutcome.Sent))
        {
            runtime.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.Sent,
                ZLinkDispatchErrorSurface.Channel,
                ZLinkDispatchMessageKind.Publish,
                PacketName: header.MessageName,
                ChannelName: channelName,
                Topic: topic,
                CorrelationId: header.CorrelationId,
                LocalRid: bundle.LocalRid,
                SocketRole: bundle.SocketRole));
        }

        return (bundle.Submitter
                ?? throw new InvalidOperationException("ZLink publish submitter is not initialized."))
            .Async(
                envelopedMsg,
                pending => publisher.Publish(topic, pending, SendFlags.DontWait),
                cancellationToken);
    }
}
