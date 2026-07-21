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

    /// <summary>Performs the first non-blocking publish admission attempt.
    /// Logical multicast reports this attempt without a send-ready retry.</summary>
    public MeshPublishResult TryPublishCurrentOnce(
        string channelName,
        string topic,
        IReadOnlyList<Message> parts,
        ReadOnlyMemory<byte> metadata)
    {
        return nativeSpot.Publish(channelName, topic, parts, SendFlags.DontWait, metadata);
    }

    internal MeshPublishResult PublishCurrentBlocking(
        string channelName,
        string topic,
        IReadOnlyList<Message> parts,
        ReadOnlyMemory<byte> metadata = default)
    {
        return nativeSpot.Publish(channelName, topic, parts, SendFlags.None, metadata);
    }

    /// <summary>Performs the first non-blocking spot-send admission attempt.
    /// False lets the async submitter wait for send-ready; routing failures
    /// surface as framework exceptions.</summary>
    public bool TrySendToSpotOnce(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        ulong targetSpotGeneration,
        IReadOnlyList<Message> parts,
        ReadOnlyMemory<byte> metadata = default)
    {
        return ZLinkSubmitFailureMapper.AcceptOrThrow(
            nativeSpot.SendToSpot(
                targetNodeRid,
                targetSpotRid,
                targetSpotGeneration,
                parts,
                SendFlags.DontWait,
                metadata),
            $"SPOT '{targetSpotRid}' on node '{targetNodeRid}'");
    }

    public ValueTask<ZLinkSubmitResult> SendToSpotAsync(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        ulong targetSpotGeneration,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        return _submitter.SubmitAsync(
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

    public ValueTask<ZLinkSubmitResult> SendToChannelAsync(
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
