namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotOutboundTransport(
    IZLinkBackendSpot nativeSpot,
    TimeSpan defaultRequestTimeout,
    TimeSpan? sendTimeout,
    CancellationToken stopToken) : IAsyncDisposable
{
    private readonly ZLinkAsyncSubmitter _submitter = new(
        nativeSpot.OnSendReady,
        sendTimeout,
        stopToken);

    public ValueTask DisposeAsync()
    {
        return _submitter.DisposeAsync();
    }

    public async ValueTask<IReadOnlyList<Message>> RequestToSpotAsync(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan? timeout,
        CancellationToken cancellationToken)
    {
        var requestTimeout = timeout ?? defaultRequestTimeout;
        return await ZLinkRawRequestSubmitter.SubmitAsync(
                _submitter,
                parts,
                (pending, callback, currentTimeout) => nativeSpot.RequestToSpot(
                    targetNodeRid,
                    targetSpotRid,
                    pending,
                    callback,
                    SendFlags.DontWait,
                    currentTimeout),
                requestTimeout,
                "SPOT request failed with result '{0}'.",
                cancellationToken)
            .ConfigureAwait(false);
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
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return nativeSpot.SendToSpot(targetRid, targetSpotRid, parts, flags);
    }
}
