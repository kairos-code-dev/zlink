using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotOutboundTransport(
    IZLinkBackendSpot nativeSpot,
    TimeSpan defaultTimeout,
    TimeSpan? sendTimeout,
    CancellationToken stopToken) : IAsyncDisposable
{
    private readonly ZLinkAsyncSubmitter _submitter = new(
        nativeSpot.OnSendReady,
        sendTimeout,
        stopToken);

    public async ValueTask<IReadOnlyList<Message>> RequestChannelAsync(
        string channelName,
        Message message,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        var requestTimeout = timeout ?? defaultTimeout;
        return await _submitter
            .SubmitRequestAsync<IReadOnlyList<Message>>(
                message,
                (pending, complete, fail) => nativeSpot.RequestChannel(
                    channelName,
                    pending,
                    (result, reply) => CompleteReply(
                        result,
                        reply,
                        complete,
                        fail,
                        $"SPOT channel request failed with result '{result}'."),
                    SendFlags.DontWait,
                    requestTimeout),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public ValueTask SendChannelAsync(
        string channelName,
        Message message,
        CancellationToken cancellationToken)
    {
        return _submitter.SubmitAsync(
            message,
            pending => nativeSpot.SendChannel(channelName, pending, SendFlags.DontWait),
            cancellationToken);
    }

    public async ValueTask<IReadOnlyList<Message>> RequestSpotAsync(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        Message message,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        var requestTimeout = timeout ?? defaultTimeout;
        return await _submitter
            .SubmitRequestAsync<IReadOnlyList<Message>>(
                message,
                (pending, complete, fail) => nativeSpot.RequestToSpot(
                    targetNodeRid,
                    targetSpotRid,
                    pending,
                    (result, reply) => CompleteReply(
                        result,
                        reply,
                        complete,
                        fail,
                        $"SPOT request failed with result '{result}'."),
                    SendFlags.DontWait,
                    requestTimeout),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public ValueTask PublishCurrentAsync(
        string topic,
        Message message,
        CancellationToken cancellationToken)
    {
        return _submitter.SubmitAsync(
            message,
            pending => nativeSpot.Publish(topic, pending, SendFlags.DontWait),
            cancellationToken);
    }

    public bool SendToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        Message message,
        SendFlags flags)
    {
        return nativeSpot.SendToSpot(targetRid, spotRid, message, flags);
    }

    public ValueTask DisposeAsync()
    {
        return _submitter.DisposeAsync();
    }

    private static void CompleteReply(
        RequestResult result,
        IReadOnlyList<Message> reply,
        Action<IReadOnlyList<Message>> complete,
        Action<Exception> fail,
        string failureMessage)
    {
        if (result == RequestResult.Ok)
        {
            complete(reply);
            return;
        }

        foreach (var replyPart in reply)
        {
            replyPart.Dispose();
        }

        fail(new TimeoutException(failureMessage));
    }
}
