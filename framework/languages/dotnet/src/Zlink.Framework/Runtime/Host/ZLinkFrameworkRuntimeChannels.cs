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

    /// <summary>
    /// True when the target is advertised as a live route mesh peer of this
    /// channel: auto connect dials such peers directly, so the route socket
    /// is the delivery path. Spot rids and unknown nodes return false and
    /// go through the spot route egress instead. Without a location store
    /// the legacy egress-first ordering applies.
    /// </summary>
    private async ValueTask<bool> IsDirectRouteMeshPeerAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        CancellationToken cancellationToken)
    {
        if (Services.GetService(typeof(IZLinkPeerLocationResolver)) is not IZLinkPeerLocationResolver peers)
        {
            return false;
        }

        try
        {
            var rows = await peers.ListPeersAsync(
                    new ZLinkPeerLocationFilter(
                        AutoConnectType: ZLinkLocationAutoConnectType.RouteMesh,
                        MeshName: routerChannelId,
                        NodeRid: targetNodeRid),
                    cancellationToken: cancellationToken)
                .ConfigureAwait(false);
            return rows.Count > 0;
        }
        catch (Exception)
        {
            // A store outage never breaks delivery; the egress path still
            // carries the message.
            return false;
        }
    }

    internal async ValueTask SubmitRouteSendAsync<TMessage>(
        string routerChannelId,
        RoutingId targetNodeRid,
        string packetName,
        TMessage message,
        CancellationToken cancellationToken)
    {
        // A directly connected route mesh peer is always the first choice;
        // the spot route egress (relay over the spot plane) serves targets
        // the route socket cannot reach, e.g. spot rids or nodes this
        // runtime never dials directly.
        var routeChannel = GetRouteChannel(routerChannelId);
        if (await IsDirectRouteMeshPeerAsync(routerChannelId, targetNodeRid, cancellationToken)
                .ConfigureAwait(false))
        {
            await routeChannel.SubmitSendAsync(
                    targetNodeRid,
                    packetName,
                    message,
                    cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        if (!routeChannel.CanDispatchRoutePacket(ZLinkMessageKind.Command, packetName)
            && _spotRouteEgress.CanHandle(routerChannelId))
        {
            var header = ZLinkClientCallCodec.CreateEnvelope(
                ZLinkMessageKind.Command,
                routerChannelId,
                packetName);
            var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(
                header,
                message,
                Registration.Codecs);
            if (await _spotRouteEgress.TrySendAsync(
                        routerChannelId,
                        targetNodeRid,
                        parts,
                        cancellationToken)
                    .ConfigureAwait(false))
                return;

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
        if (await IsDirectRouteMeshPeerAsync(routerChannelId, targetNodeRid, cancellationToken)
                .ConfigureAwait(false))
        {
            return await routeChannel.RequestAsync<TRequest, TReply>(
                    targetNodeRid,
                    packetName,
                    request,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        if (!routeChannel.CanDispatchRoutePacket(ZLinkMessageKind.Request, packetName)
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
                Registration.Codecs);
            var result = await _spotRouteEgress.TryRequestAsync(
                    routerChannelId,
                    targetNodeRid,
                    parts,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
            if (result.WasHandled)
                return ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<TReply>(
                    result.Reply,
                    "Route SPOT reply was empty.",
                    $"Route SPOT request failed for '{packetName}'.",
                    Registration.Codecs);

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
