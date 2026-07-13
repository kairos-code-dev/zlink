namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotOutboundTransport(
    IZLinkBackendSpot nativeSpot,
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

    public ValueTask SendToSpotAsync(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        return _submitter.Async(
            parts,
            pending => nativeSpot.SendToSpot(
                targetNodeRid,
                targetSpotRid,
                pending,
                SendFlags.DontWait),
            cancellationToken);
    }

    public ValueTask<IReadOnlyList<Message>> RequestToSpotAsync(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return _submitter.SubmitRequestAsync<IReadOnlyList<Message>>(
            parts,
            (pending, complete, fail) => nativeSpot.RequestToSpot(
                targetNodeRid,
                targetSpotRid,
                pending,
                (result, reply) =>
                {
                    if (result == RequestResult.Ok)
                    {
                        complete(reply);
                        return;
                    }

                    fail(ZLinkRequestFailureMapper.CreateCompletionException(
                        result,
                        $"SPOT request to '{targetSpotRid}' on node '{targetNodeRid}' failed with result '{result}'."));
                    ZLinkMessageParts.DisposeAll(reply);
                },
                SendFlags.None,
                timeout),
            cancellationToken,
            ZLinkMessageParts.DisposeAll);
    }

}
