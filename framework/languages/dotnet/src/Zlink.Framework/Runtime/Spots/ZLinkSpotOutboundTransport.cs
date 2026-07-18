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

    /// <summary>One-shot non-blocking publish (TrySubmit surface): a single
    /// DontWait attempt with no send-ready wait. False = backpressured.</summary>
    public bool TryPublishCurrentOnce(
        string topic,
        IReadOnlyList<Message> parts,
        ReadOnlyMemory<byte> metadata,
        out MeshPublishDetail? detail)
    {
        return TryPublish(topic, parts, metadata, out detail);
    }

    public async ValueTask<MeshPublishDetail> PublishCurrentAsync(
        string topic,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        MeshPublishDetail? detail = null;
        await _submitter.Async(
                parts,
                pending => TryPublish(topic, pending, metadata, out detail),
                cancellationToken)
            .ConfigureAwait(false);
        return detail
               ?? throw new InvalidOperationException(
                   "Publish completed without a fan-out detail.");
    }

    private bool TryPublish(
        string topic,
        IReadOnlyList<Message> parts,
        ReadOnlyMemory<byte> metadata,
        out MeshPublishDetail? detail)
    {
        try
        {
            detail = nativeSpot.Publish(topic, parts, SendFlags.DontWait, metadata);
            return true;
        }
        catch (ZlinkSubmitException ex) when
            (ex.Result == ZlinkSubmitException.ErrorCode.Backpressured)
        {
            detail = null;
            return false;
        }
    }

    public ValueTask SendToSpotAsync(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        ulong targetSpotGeneration,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        return _submitter.Async(
            parts,
            pending => ZLinkSubmitFailureMapper.AcceptOrThrow(
                nativeSpot.SendToSpot(
                    targetNodeRid,
                    targetSpotRid,
                    targetSpotGeneration,
                    pending,
                    SendFlags.DontWait,
                    metadata),
                $"SPOT '{targetSpotRid}' on node '{targetNodeRid}'"),
            cancellationToken);
    }

    public ValueTask<IReadOnlyList<Message>> RequestToSpotAsync(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        ulong targetSpotGeneration,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        return _submitter.SubmitRequestAsync<IReadOnlyList<Message>>(
            parts,
            (pending, complete, fail) => nativeSpot.RequestToSpot(
                targetNodeRid,
                targetSpotRid,
                targetSpotGeneration,
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
                timeout,
                metadata),
            cancellationToken,
            ZLinkMessageParts.DisposeAll);
    }

}
