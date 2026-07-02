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

    // EHOSTUNREACH from a mandatory router send: the routing id is not a
    // directly connected peer of the route socket.
    private const int HostUnreachableErrno = 113;

    internal async ValueTask SubmitRouteSendAsync<TMessage>(
        string routerChannelId,
        RoutingId targetNodeRid,
        string packetName,
        TMessage message,
        CancellationToken cancellationToken)
    {
        // A directly connected route mesh peer is always the first choice;
        // the spot route egress (relay over the spot plane) only serves
        // targets the route socket cannot reach, e.g. spot rids or nodes
        // this runtime never dials directly.
        var routeChannel = GetRouteChannel(routerChannelId);
        try
        {
            await routeChannel.SubmitSendAsync(
                    targetNodeRid,
                    packetName,
                    message,
                    cancellationToken)
                .ConfigureAwait(false);
            return;
        }
        catch (ZlinkException exception) when (
            exception.NativeErrno == HostUnreachableErrno
            && !routeChannel.CanDispatchRoutePacket(ZLinkMessageKind.Command, packetName)
            && _spotRouteEgress.CanHandle(routerChannelId))
        {
        }

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
        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            $"Route channel '{routerChannelId}' has no path to '{targetNodeRid}' for '{packetName}'.");
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
        try
        {
            return await routeChannel.RequestAsync<TRequest, TReply>(
                    targetNodeRid,
                    packetName,
                    request,
                    timeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (ZlinkException exception) when (
            exception.NativeErrno == HostUnreachableErrno
            && !routeChannel.CanDispatchRoutePacket(ZLinkMessageKind.Request, packetName)
            && _spotRouteEgress.CanHandle(routerChannelId))
        {
        }

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
        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorRouteNotFound,
            $"Route channel '{routerChannelId}' has no path to '{targetNodeRid}' for '{packetName}'.");
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
