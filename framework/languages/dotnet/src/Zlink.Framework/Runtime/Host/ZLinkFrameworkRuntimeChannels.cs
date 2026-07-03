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
    /// Classifies the target from the auto-connect reconciler's desired-set
    /// snapshot — never from the store, so the send path stays free of
    /// hidden store I/O. True: a known route mesh peer, the route socket is
    /// the delivery path. False: the mesh does not know this rid (a spot
    /// rid or a stale node) — the egress owns it. Null: no loop manages
    /// this mesh yet, keep the legacy ordering.
    /// </summary>
    private bool? IsKnownRouteMeshPeer(string routerChannelId, RoutingId targetNodeRid)
    {
        return Services.GetService(typeof(IZLinkAutoConnectTopologyQuery))
            is IZLinkAutoConnectTopologyQuery topology
            ? topology.IsKnownRouteMeshPeer(routerChannelId, targetNodeRid)
            : null;
    }

    internal async ValueTask SubmitRouteSendAsync<TMessage>(
        string routerChannelId,
        RoutingId targetNodeRid,
        string packetName,
        TMessage message,
        CancellationToken cancellationToken)
    {
        // Route channel sends target node rids only; spot-addressed
        // traffic goes through the address-based spot outbound instead
        // (spot-address messaging draft §6).
        var routeChannel = GetRouteChannel(routerChannelId);
        var known = IsKnownRouteMeshPeer(routerChannelId, targetNodeRid);
        try
        {
            await routeChannel.SubmitSendAsync(
                    targetNodeRid,
                    packetName,
                    message,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (ZLinkFrameworkException exception) when (
            exception.Kind == ZLinkFrameworkErrorKind.RouteNotConnected && known == false)
        {
            // The mesh does not know this rid at all: the address is stale
            // or wrong, not merely unconverged. Retrying the send cannot
            // help; the caller must re-resolve.
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestTargetNotFound,
                $"Route channel '{routerChannelId}' does not know node '{targetNodeRid}' for '{packetName}'.",
                innerException: exception);
        }
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
        var known = IsKnownRouteMeshPeer(routerChannelId, targetNodeRid);
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
        catch (ZLinkFrameworkException exception) when (
            exception.Kind == ZLinkFrameworkErrorKind.RouteNotConnected && known == false)
        {
            // The mesh does not know this rid at all: the address is stale
            // or wrong, not merely unconverged. Retrying the request cannot
            // help; the caller must re-resolve.
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestTargetNotFound,
                $"Route channel '{routerChannelId}' does not know node '{targetNodeRid}' for '{packetName}'.",
                innerException: exception);
        }
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
