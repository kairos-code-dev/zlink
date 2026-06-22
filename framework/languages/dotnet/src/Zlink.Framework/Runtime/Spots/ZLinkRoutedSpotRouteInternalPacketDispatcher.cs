namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkRoutedSpotRouteInternalPacketDispatcher(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration)
    : IZLinkRouteInternalPacketDispatcher
{
    public bool CanHandleSend(string packetName)
    {
        return packetName == ZLinkRoutedSpotRelayPackets.SendPacketName;
    }

    public bool CanHandleRequest(string packetName)
    {
        return packetName == ZLinkRoutedSpotRelayPackets.RequestPacketName;
    }

    public async ValueTask DispatchSendAsync(
        Received received,
        CancellationToken cancellationToken)
    {
        var header = ZLinkEnvelopeCodec.DecodeHeader(received.Parts);
        var metadata = ZLinkRoutedSpotRelayPackets.DecodeMetadata(received);
        var targetNodeRid = runtime.ResolveAcceptedSpotRouteNodeRid(header.ChannelName);
        var targetSpotRid = RoutingId.From(metadata.TargetSpotRid);
        var spotParts = ZLinkRoutedSpotRelayPackets.CopySpotPayloadParts(received);
        try
        {
            await runtime.SendToSpotViaRouterChannelAsync(
                    header.ChannelName,
                    targetNodeRid,
                    targetSpotRid,
                    spotParts,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(spotParts);
        }
    }

    public async ValueTask<Message> DispatchRequestAsync(
        Received received,
        ZLinkEnvelopeHeader routedHeader,
        CancellationToken cancellationToken)
    {
        var metadata = ZLinkRoutedSpotRelayPackets.DecodeMetadata(received);
        var targetNodeRid = runtime.ResolveAcceptedSpotRouteNodeRid(routedHeader.ChannelName);
        var targetSpotRid = RoutingId.From(metadata.TargetSpotRid);
        var timeout = ResolveInternalTimeout(routedHeader);
        var spotParts = ZLinkRoutedSpotRelayPackets.CopySpotPayloadParts(received);
        IReadOnlyList<Message> reply;
        try
        {
            reply = await runtime.RequestToSpotViaRouterChannelAsync(
                    routedHeader.ChannelName,
                    targetNodeRid,
                    targetSpotRid,
                    spotParts,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(spotParts);
        }

        try
        {
            return ZLinkEnvelopeCodec.EncodePart(ZLinkRoutedSpotRelayReply.FromMessages(reply));
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(reply);
        }
    }

    private TimeSpan ResolveInternalTimeout(ZLinkEnvelopeHeader header)
    {
        if (header.Deadline is not { } deadline)
        {
            return registration.DefaultRequestTimeout;
        }

        var remaining = deadline - DateTimeOffset.UtcNow;
        return remaining > TimeSpan.Zero
            ? remaining
            : TimeSpan.FromMilliseconds(1);
    }
}
