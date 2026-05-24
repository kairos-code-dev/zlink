using Zlink.Framework.Runtime.Messaging;

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
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            _channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."));

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
        var timeout = _timeout ?? registration.DefaultTimeout;
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            timeout);
        var message = ZLinkClientCallCodec.EncodeEnvelopeParts(header, request);
        if (registration.Channels.TryGetValue(channelName, out var channel)
            && channel.AutoConnectType == ZLinkAutoConnectType.DealerMesh)
        {
            return await SubmitDealerMeshRequestAsync<TReply>(
                    bundle,
                    dealer,
                    message,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        return await (bundle.Submitter
            ?? throw new InvalidOperationException("ZLink request submitter is not initialized."))
            .SubmitRequestAsync<TReply>(
                message,
                (pending, complete, fail) => dealer.Request(
                    pending,
                    (result, reply) => ZLinkEnvelopeReplyCompletion.Complete(
                        result,
                        reply,
                        complete,
                        fail,
                        "ZLink request"),
                    SendFlags.DontWait,
                    timeout),
                cancellationToken)
            .ConfigureAwait(false);
    }

    private static async ValueTask<TReply> SubmitDealerMeshRequestAsync<TReply>(
        ZLinkChannelRuntimeBundle bundle,
        IZLinkBackendDealerSocket dealer,
        IReadOnlyList<Message> message,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var requestSeq = bundle.DealerMeshPendingRequests.NextRequestSeq();
        var completion = new TaskCompletionSource<TReply>(
            TaskCreationOptions.RunContinuationsAsynchronously);

        if (!bundle.DealerMeshPendingRequests.TryRegister(
                requestSeq,
                reply => ZLinkEnvelopeReplyCompletion.Complete<TReply>(
                    RequestResult.Ok,
                    reply,
                    result => completion.TrySetResult(result),
                    error => completion.TrySetException(error),
                    "ZLink dealer mesh request")))
        {
            throw new InvalidOperationException(
                $"DealerMesh request sequence '{requestSeq}' is already pending.");
        }

        try
        {
            await (bundle.Submitter
                    ?? throw new InvalidOperationException("ZLink request submitter is not initialized."))
                .SubmitAsync(
                    message,
                    pending => dealer.RequestFrame(
                        requestSeq,
                        pending,
                        SendFlags.DontWait),
                    cancellationToken)
                .ConfigureAwait(false);

            var timeoutTask = Task.Delay(timeout, cancellationToken);
            var completed = await Task.WhenAny(completion.Task, timeoutTask)
                .ConfigureAwait(false);
            if (completed == completion.Task)
            {
                return await completion.Task.ConfigureAwait(false);
            }

            bundle.DealerMeshPendingRequests.Remove(requestSeq);
            cancellationToken.ThrowIfCancellationRequested();
            throw new TimeoutException("ZLink dealer mesh request timed out.");
        }
        catch
        {
            bundle.DealerMeshPendingRequests.Remove(requestSeq);
            throw;
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
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Publish,
            channelName,
            _messageName ?? throw new InvalidOperationException("Message name is required."),
            topic: topic,
            source: channelName);
        var envelopedMsg = ZLinkEnvelopeCodec.EncodeParts(header, message, message?.GetType());
        return (bundle.Submitter
                ?? throw new InvalidOperationException("ZLink publish submitter is not initialized."))
            .SubmitAsync(
                envelopedMsg,
                pending => publisher.Publish(topic, pending, SendFlags.DontWait),
                cancellationToken);
    }
}
