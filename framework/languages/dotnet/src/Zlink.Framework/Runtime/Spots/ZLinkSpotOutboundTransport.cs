using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotOutboundTransport(
    IZLinkBackendSpot nativeSpot,
    TimeSpan defaultRequestTimeout,
    TimeSpan? sendTimeout,
    CancellationToken stopToken,
    Func<string, ZLinkSpotAttachedChannelBundle?>? channelClient = null) : IAsyncDisposable
{
    private readonly ZLinkAsyncSubmitter _submitter = new(
        nativeSpot.OnSendReady,
        sendTimeout,
        stopToken);

    public async ValueTask<IReadOnlyList<Message>> RequestToChannelAsync(
        string channelName,
        Message message,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        var requestTimeout = timeout ?? defaultRequestTimeout;
        var attached = channelClient?.Invoke(channelName);
        if (attached is not null)
        {
            return await attached.Submitter
                .SubmitRequestAsync<IReadOnlyList<Message>>(
                    message,
                    (pending, complete, fail) => attached.Socket.Request(
                        pending,
                        (result, reply) => ZLinkRawReplyCompletion.Complete(
                            result,
                            reply,
                            complete,
                            fail,
                            $"SPOT attached channel request failed with result '{result}'."),
                        SendFlags.DontWait,
                        requestTimeout),
                    cancellationToken)
                .ConfigureAwait(false);
        }

        return await _submitter
            .SubmitRequestAsync<IReadOnlyList<Message>>(
                message,
                (pending, complete, fail) => nativeSpot.RequestToChannel(
                    channelName,
                    pending,
                    (result, reply) => ZLinkRawReplyCompletion.Complete(
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

    public async ValueTask<IReadOnlyList<Message>> RequestToChannelAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        var requestTimeout = timeout ?? defaultRequestTimeout;
        var attached = channelClient?.Invoke(channelName);
        if (attached is not null)
        {
            return await attached.Submitter
                .SubmitRequestAsync<IReadOnlyList<Message>>(
                    parts,
                    (pending, complete, fail) => attached.Socket.Request(
                        pending,
                        (result, reply) => ZLinkRawReplyCompletion.Complete(
                            result,
                            reply,
                            complete,
                            fail,
                            $"SPOT attached channel request failed with result '{result}'."),
                        SendFlags.DontWait,
                        requestTimeout),
                    cancellationToken)
                .ConfigureAwait(false);
        }

        return await _submitter
            .SubmitRequestAsync<IReadOnlyList<Message>>(
                parts,
                (pending, complete, fail) => nativeSpot.RequestToChannel(
                    channelName,
                    pending,
                    (result, reply) => ZLinkRawReplyCompletion.Complete(
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

    public ValueTask SendToChannelAsync(
        string channelName,
        Message message,
        CancellationToken cancellationToken)
    {
        var attached = channelClient?.Invoke(channelName);
        return attached is not null
            ? attached.Submitter.Async(
                message,
                pending => attached.Socket.Send(pending, SendFlags.DontWait),
                cancellationToken)
            : _submitter.Async(
                message,
                pending => nativeSpot.SendToChannel(channelName, pending, SendFlags.DontWait),
                cancellationToken);
    }

    public ValueTask SendToChannelAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        var attached = channelClient?.Invoke(channelName);
        return attached is not null
            ? attached.Submitter.Async(
                parts,
                pending => attached.Socket.Send(pending, SendFlags.DontWait),
                cancellationToken)
            : _submitter.Async(
                parts,
                pending => nativeSpot.SendToChannel(channelName, pending, SendFlags.DontWait),
                cancellationToken);
    }

    public async ValueTask<IReadOnlyList<Message>> RequestToSpotAsync(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        Message message,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        var requestTimeout = timeout ?? defaultRequestTimeout;
        return await _submitter
            .SubmitRequestAsync<IReadOnlyList<Message>>(
                message,
                (pending, complete, fail) => nativeSpot.RequestToSpot(
                    targetNodeRid,
                    targetSpotRid,
                    pending,
                    (result, reply) => ZLinkRawReplyCompletion.Complete(
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

    public async ValueTask<IReadOnlyList<Message>> RequestToSpotAsync(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        var requestTimeout = timeout ?? defaultRequestTimeout;
        return await _submitter
            .SubmitRequestAsync<IReadOnlyList<Message>>(
                parts,
                (pending, complete, fail) => nativeSpot.RequestToSpot(
                    targetNodeRid,
                    targetSpotRid,
                    pending,
                    (result, reply) => ZLinkRawReplyCompletion.Complete(
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
        return _submitter.Async(
            message,
            pending => nativeSpot.Publish(topic, pending, SendFlags.DontWait),
            cancellationToken);
    }

    public ValueTask PublishCurrentAsync(
        string topic,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        return _submitter.Async(
            parts,
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

    public bool SendToSpot(
        RoutingId targetRid,
        RoutingId spotRid,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return nativeSpot.SendToSpot(targetRid, spotRid, parts, flags);
    }

    public ValueTask DisposeAsync()
    {
        return _submitter.DisposeAsync();
    }
}
