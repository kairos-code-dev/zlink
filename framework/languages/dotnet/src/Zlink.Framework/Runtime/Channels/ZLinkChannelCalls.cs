namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkSendCall : IZLinkSendCall
{
    private readonly ZLinkFrameworkRuntime _runtime;
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
        _channelName = channelName;
        _message = message;
        _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);
        _ = registration;
    }

    public IZLinkSendCall PacketName(string messageName)
    {
        _messageName = messageName;
        return this;
    }

    public ValueTask Submit(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var bundle = _runtime.GetOrCreateClientBundle(_channelName);
        var dealer = (IZLinkBackendDealerSocket)bundle.Socket;
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Command,
            _channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            Guid.NewGuid().ToString("N"),
            null,
            null,
            null,
            null);

        var message = ZLinkEnvelopeCodec.EncodeParts(header, _message, _message?.GetType());
        return (bundle.Submitter
                ?? throw new InvalidOperationException("ZLink send submitter is not initialized."))
            .SubmitAsync(
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

    public async ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default)
    {
        var bundle = runtime.GetOrCreateClientBundle(channelName);
        var dealer = (IZLinkBackendDealerSocket)bundle.Socket;
        var correlationId = Guid.NewGuid().ToString("N");
        var timeout = _timeout ?? registration.DefaultTimeout;
        var deadline = DateTimeOffset.UtcNow.Add(timeout);
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            correlationId,
            deadline,
            null,
            null,
            null);
        var message = ZLinkEnvelopeCodec.EncodeParts(header, request, request?.GetType() ?? typeof(TMessage));
        return await (bundle.Submitter
                ?? throw new InvalidOperationException("ZLink request submitter is not initialized."))
            .SubmitRequestAsync<TReply>(
                message,
                (pending, complete, fail) => dealer.Request(
                    pending,
                    (result, reply) => CompleteReply(result, reply, complete, fail),
                    SendFlags.DontWait,
                    timeout),
                cancellationToken)
            .ConfigureAwait(false);
    }

    private static void CompleteReply<TReply>(
        RequestResult result,
        IReadOnlyList<Message> reply,
        Action<TReply> complete,
        Action<Exception> fail)
    {
        try
        {
            if (result != RequestResult.Ok)
            {
                fail(new TimeoutException($"ZLink request failed with result '{result}'."));
                return;
            }

            if (reply.Count == 0)
            {
                fail(new InvalidOperationException("ZLink request reply is empty."));
                return;
            }

            var replyHeader = ZLinkEnvelopeCodec.DecodeHeader(reply);
            if (replyHeader.Kind == ZLinkMessageKind.Error)
            {
                fail(new InvalidOperationException(replyHeader.ErrorMessage ?? "ZLink request failed."));
                return;
            }

            complete((TReply?)ZLinkEnvelopeCodec.DecodeBody(reply, typeof(TReply))
                ?? throw new InvalidOperationException("ZLink request reply body is null."));
        }
        catch (Exception exception)
        {
            fail(exception);
        }
        finally
        {
            foreach (var replyPart in reply)
            {
                replyPart.Dispose();
            }
        }
    }
}

internal sealed class ZLinkPublishCall(
    ZLinkFrameworkRuntime runtime,
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

    public ValueTask Submit(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var bundle = runtime.GetOrCreatePublisherBundle(channelName);
        var publisher = (IZLinkBackendPublisherSocket)bundle.Socket;
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Publish,
            channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            Guid.NewGuid().ToString("N"),
            null,
            topic,
            null,
            null,
            Source: channelName);
        var envelopedMsg = ZLinkEnvelopeCodec.EncodeParts(header, message, message?.GetType());
        return (bundle.Submitter
                ?? throw new InvalidOperationException("ZLink publish submitter is not initialized."))
            .SubmitAsync(
                envelopedMsg,
                pending => publisher.Publish(topic, pending, SendFlags.DontWait),
                cancellationToken);
    }
}
