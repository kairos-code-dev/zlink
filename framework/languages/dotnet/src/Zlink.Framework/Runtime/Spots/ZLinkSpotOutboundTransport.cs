using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotOutboundTransport(
    IZLinkBackendSpot nativeSpot,
    TimeSpan defaultTimeout,
    TimeSpan? sendTimeout,
    CancellationToken stopToken,
    Func<string, ZLinkAsyncSubmitter?>? channelSubmitter = null) : IAsyncDisposable
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
        return await ResolveSubmitter(channelName)
            .SubmitRequestAsync<IReadOnlyList<Message>>(
                message,
                (pending, complete, fail) => nativeSpot.RequestChannel(
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

    public async ValueTask<IReadOnlyList<Message>> RequestChannelAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        var requestTimeout = timeout ?? defaultTimeout;
        return await ResolveSubmitter(channelName)
            .SubmitRequestAsync<IReadOnlyList<Message>>(
                parts,
                (pending, complete, fail) => nativeSpot.RequestChannel(
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

    public ValueTask SendChannelAsync(
        string channelName,
        Message message,
        CancellationToken cancellationToken)
    {
        return ResolveSubmitter(channelName).SubmitAsync(
            message,
            pending => nativeSpot.SendChannel(channelName, pending, SendFlags.DontWait),
            cancellationToken);
    }

    public ValueTask SendChannelAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        return ResolveSubmitter(channelName).SubmitAsync(
            parts,
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

    public async ValueTask<IReadOnlyList<Message>> RequestSpotAsync(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        var requestTimeout = timeout ?? defaultTimeout;
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
        return _submitter.SubmitAsync(
            message,
            pending => nativeSpot.Publish(topic, pending, SendFlags.DontWait),
            cancellationToken);
    }

    public ValueTask PublishCurrentAsync(
        string topic,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        return _submitter.SubmitAsync(
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

    private ZLinkAsyncSubmitter ResolveSubmitter(string channelName)
    {
        return channelSubmitter?.Invoke(channelName) ?? _submitter;
    }

}
