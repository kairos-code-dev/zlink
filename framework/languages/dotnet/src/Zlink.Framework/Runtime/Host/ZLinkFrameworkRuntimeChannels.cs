namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    internal ZLinkChannelRuntimeBundle GetOrCreateClientBundle(string channelName)
    {
        return _channelFacade.GetOrCreateClientBundle(channelName);
    }

    internal ZLinkChannelRuntimeBundle GetOrCreatePublisherBundle(string channelName)
    {
        return _channelFacade.GetOrCreatePublisherBundle(channelName);
    }

    internal ZLinkRouteChannelRuntime GetRouteChannel(string routerChannelId)
    {
        return _channelFacade.GetRouteChannel(routerChannelId);
    }

    internal async ValueTask SubmitRouteSendAsync<TMessage>(
        string routerChannelId,
        RoutingId targetNodeRid,
        string packetName,
        TMessage message,
        CancellationToken cancellationToken)
    {
        var routeChannel = GetRouteChannel(routerChannelId);
        if (!routeChannel.CanDispatchRoutePacket(ZLinkMessageKind.Command, packetName)
            && !routeChannel.HasKnownRoutePeer(targetNodeRid)
            && _spotRouteEgress.CanHandle(routerChannelId))
        {
            var header = ZLinkClientCallCodec.CreateEnvelope(
                ZLinkMessageKind.Command,
                routerChannelId,
                packetName);
            var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(
                header,
                message,
                _registration.Codecs);
            if (await _spotRouteEgress.TrySendAsync(
                    routerChannelId,
                    targetNodeRid,
                    parts,
                    cancellationToken)
                .ConfigureAwait(false))
            {
                return;
            }

            ZLinkMessageParts.DisposeAll(parts);
        }

        await routeChannel.SubmitSendAsync(
                targetNodeRid,
                packetName,
                message,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask<TReply> SubmitRouteRequestAsync<TRequest, TReply>(
        string routerChannelId,
        RoutingId targetNodeRid,
        string packetName,
        TRequest request,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var routeChannel = GetRouteChannel(routerChannelId);
        if (!routeChannel.CanDispatchRoutePacket(ZLinkMessageKind.Request, packetName)
            && !routeChannel.HasKnownRoutePeer(targetNodeRid)
            && _spotRouteEgress.CanHandle(routerChannelId))
        {
            var header = ZLinkClientCallCodec.CreateEnvelope(
                ZLinkMessageKind.Request,
                routerChannelId,
                packetName,
                timeout);
            var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(
                header,
                request,
                _registration.Codecs);
            var result = await _spotRouteEgress.TryRequestAsync(
                    routerChannelId,
                    targetNodeRid,
                    parts,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
            if (result.WasHandled)
            {
                return ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<TReply>(
                    result.Reply,
                    "Route SPOT reply was empty.",
                    $"Route SPOT request failed for '{packetName}'.",
                    _registration.Codecs);
            }

            ZLinkMessageParts.DisposeAll(parts);
        }

        return await routeChannel.RequestAsync<TRequest, TReply>(
                targetNodeRid,
                packetName,
                request,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask SendToSpotViaRouterChannelAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        await _spotRouteRouter.SendAsync(
            routerChannelId,
            targetNodeRid,
            targetSpotRid,
            parts,
            cancellationToken).ConfigureAwait(false);
    }

    internal RoutingId ResolveAcceptedSpotRouteNodeRid(string targetSpotNodeChannelName)
    {
        return _spotRouteRouter.ResolveAcceptedSpotRouteNodeRid(targetSpotNodeChannelName);
    }

    internal async ValueTask<IReadOnlyList<Message>> RequestToSpotViaRouterChannelAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return await _spotRouteRouter.RequestAsync(
                routerChannelId,
                targetNodeRid,
                targetSpotRid,
                parts,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal IZLinkBackendSocket GetMonitoringSocket(string sourceName)
    {
        return _channelFacade.GetMonitoringSocket(sourceName);
    }
}
