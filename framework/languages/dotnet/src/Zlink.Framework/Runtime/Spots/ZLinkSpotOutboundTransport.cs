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

    internal void PublishCurrent(
        string channelName,
        string topic,
        IReadOnlyList<Message> parts,
        ReadOnlyMemory<byte> metadata = default)
    {
        nativeSpot.Publish(channelName, topic, parts, SendFlags.None, metadata);
    }

    /// <summary>Performs the first non-blocking spot-send admission attempt.
    /// False lets the async submitter wait for send-ready; routing failures
    /// surface as framework exceptions.</summary>
    public bool TrySendToSpotOnce(
        RoutingId targetNodeRid,
        string targetSpotId,
        ulong targetSpotGeneration,
        ulong authorityOwnerGeneration,
        IReadOnlyList<Message> parts,
        ReadOnlyMemory<byte> metadata = default)
    {
        ObserveSpotAuthority(
            targetNodeRid,
            targetSpotId,
            targetSpotGeneration,
            authorityOwnerGeneration);
        return ZLinkSubmitFailureMapper.AcceptOrThrow(
            nativeSpot.SendToSpot(
                targetNodeRid,
                targetSpotId,
                targetSpotGeneration,
                parts,
                SendFlags.DontWait,
                metadata),
            $"SPOT '{targetSpotId}' on node '{targetNodeRid}'");
    }

    public ValueTask<ZLinkOneWaySubmitResult> SendToSpotAsync(
        RoutingId targetNodeRid,
        string targetSpotId,
        ulong targetSpotGeneration,
        ulong authorityOwnerGeneration,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        ObserveSpotAuthority(
            targetNodeRid,
            targetSpotId,
            targetSpotGeneration,
            authorityOwnerGeneration);
        return _submitter.SubmitAsync(
            parts,
            pending => ZLinkSubmitFailureMapper.AcceptOrThrow(
                nativeSpot.SendToSpot(
                    targetNodeRid,
                    targetSpotId,
                    targetSpotGeneration,
                    pending,
                    SendFlags.DontWait,
                    metadata),
                $"SPOT '{targetSpotId}' on node '{targetNodeRid}'"),
            cancellationToken);
    }

    private void ObserveSpotAuthority(
        RoutingId targetNodeRid,
        string targetSpotId,
        ulong targetSpotGeneration,
        ulong authorityOwnerGeneration)
    {
        if (targetNodeRid == default || authorityOwnerGeneration == 0)
            return;
        if (nativeSpot is not IZLinkBackendAuthorityObserver observer)
            throw new InvalidOperationException(
                "The Spot backend does not support authority fencing.");
        observer.ObserveSpotAuthority(
            targetNodeRid,
            targetSpotId,
            targetSpotGeneration,
            authorityOwnerGeneration);
    }

    /// <summary>Performs the first non-blocking ChannelName select-one
    /// admission attempt. False lets the async submitter wait for
    /// send-ready.</summary>
    public bool TrySendToChannelOnce(
        string channelName,
        IReadOnlyList<Message> parts,
        ReadOnlyMemory<byte> metadata = default)
    {
        return ZLinkSubmitFailureMapper.AcceptOrThrow(
            nativeSpot.SendToChannel(channelName, parts, SendFlags.DontWait, metadata),
            $"channel '{channelName}'");
    }

    public ValueTask<ZLinkOneWaySubmitResult> SendToChannelAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        return _submitter.SubmitAsync(
            parts,
            pending => ZLinkSubmitFailureMapper.AcceptOrThrow(
                nativeSpot.SendToChannel(channelName, pending, SendFlags.DontWait, metadata),
                $"channel '{channelName}'"),
            cancellationToken);
    }

    public ValueTask<IReadOnlyList<Message>> RequestToChannelAsync(
        string channelName,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        return _submitter.SubmitRequestAsync<IReadOnlyList<Message>>(
            parts,
            (pending, complete, fail) => nativeSpot.RequestToChannel(
                channelName,
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
                        $"Channel request to '{channelName}' failed with result '{result}'."));
                    ZLinkMessageParts.DisposeAll(reply);
                },
                SendFlags.None,
                timeout,
                metadata),
            cancellationToken,
            ZLinkMessageParts.DisposeAll);
    }

    public ValueTask<IReadOnlyList<Message>> RequestToSpotAsync(
        RoutingId targetNodeRid,
        string targetSpotId,
        ulong targetSpotGeneration,
        ulong authorityOwnerGeneration,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        ObserveSpotAuthority(
            targetNodeRid,
            targetSpotId,
            targetSpotGeneration,
            authorityOwnerGeneration);
        return _submitter.SubmitRequestAsync<IReadOnlyList<Message>>(
            parts,
            (pending, complete, fail) => nativeSpot.RequestToSpot(
                targetNodeRid,
                targetSpotId,
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
                        $"SPOT request to '{targetSpotId}' on node '{targetNodeRid}' failed with result '{result}'."));
                    ZLinkMessageParts.DisposeAll(reply);
                },
                SendFlags.None,
                timeout,
                metadata),
            cancellationToken,
            ZLinkMessageParts.DisposeAll);
    }

}
