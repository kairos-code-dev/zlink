namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    internal ZLinkChannelRuntimeBundle GetPublisherBundle(string channelName)
    {
        return _channels.GetPublisherBundle(GetOrStartState(), channelName);
    }

    /// <summary>
    /// Classifies the target from the auto-connect reconciler's desired-set
    /// snapshot — never from the store, so the send path stays free of
    /// hidden store I/O. True: a known route mesh peer, the route socket is
    /// the delivery path. False: the mesh does not know this rid (a spot
    /// rid or a stale node) — the egress owns it. Null: no loop manages
    /// this mesh yet, keep the default ordering.
    /// </summary>
    private bool? IsKnownRouteMeshPeer(string routerChannelId, RoutingId targetNodeRid)
    {
        return _topologyQuery?.IsKnownRouteMeshPeer(routerChannelId, targetNodeRid);
    }

    internal void EnsureKnownRouteMeshPeer(
        string routerChannelId,
        RoutingId targetNodeRid,
        string targetDescription)
    {
        var nodeRuntime = GetMeshNodeRuntime(routerChannelId);
        if (nodeRuntime.Node.RoutingId == targetNodeRid) return;

        if (nodeRuntime.UsesManualRouterAcquisition)
        {
            if (nodeRuntime.TryClassifyManualRouterTarget(targetNodeRid, out var connected))
            {
                if (connected) return;
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RouteNotConnected,
                    $"Route channel '{routerChannelId}' is not connected to node '{targetNodeRid}' for {targetDescription}.",
                    isRetriable: true);
            }

            throw CreateUnknownRouteTargetException(
                routerChannelId, targetNodeRid, targetDescription);
        }

        if (IsKnownRouteMeshPeer(routerChannelId, targetNodeRid) != false) return;

        throw CreateUnknownRouteTargetException(routerChannelId, targetNodeRid, targetDescription);
    }

    internal async ValueTask<ZLinkSubmitResult> SendToSpotViaRouterChannelAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        ulong targetSpotGeneration,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        using var operation = EnterOperation();
        var handedOff = false;
        try
        {
            EnsureKnownRouteMeshPeer(routerChannelId, targetNodeRid, $"SPOT '{targetSpotRid}'");

            var accepted = _spotRouteRouter.SendAsync(
                routerChannelId,
                targetNodeRid,
                targetSpotRid,
                targetSpotGeneration,
                parts,
                cancellationToken,
                metadata);
            handedOff = true;
            return await accepted.ConfigureAwait(false);
        }
        catch
        {
            if (!handedOff) ZLinkMessageParts.DisposeAll(parts);
            throw;
        }
    }

    internal RoutingId ResolveAcceptedSpotRouteNodeRid(string targetSpotNodeChannelName)
    {
        return _spotRouteRouter.ResolveAcceptedSpotRouteNodeRid(targetSpotNodeChannelName);
    }

    /// <summary>Performs the first non-blocking spot-send admission attempt
    /// after the route-mesh peer check.</summary>
    internal bool TrySendToSpotViaRouterChannelOnce(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        ulong targetSpotGeneration,
        IReadOnlyList<Message> parts,
        ReadOnlyMemory<byte> metadata = default)
    {
        using var operation = EnterOperation();
        EnsureKnownRouteMeshPeer(routerChannelId, targetNodeRid, $"SPOT '{targetSpotRid}'");
        return _spotRouteRouter.TrySendOnce(
            routerChannelId,
            targetNodeRid,
            targetSpotRid,
            targetSpotGeneration,
            parts,
            metadata);
    }

    internal async ValueTask<IReadOnlyList<Message>> RequestToSpotViaRouterChannelAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        ulong targetSpotGeneration,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        try
        {
            using var operation = EnterOperation(countAsRequest: true);
            var metricStarted = ZLinkRuntimeMetrics.StartChannelRequest();
            var timedOut = false;
            try
            {
                EnsureKnownRouteMeshPeer(routerChannelId, targetNodeRid, $"SPOT '{targetSpotRid}'");

                return await _spotRouteRouter.RequestAsync(
                        routerChannelId,
                        targetNodeRid,
                        targetSpotRid,
                        targetSpotGeneration,
                        parts,
                        timeout,
                        cancellationToken,
                        metadata)
                    .ConfigureAwait(false);
            }
            catch (TimeoutException)
            {
                timedOut = true;
                throw;
            }
            finally
            {
                ZLinkRuntimeMetrics.CompleteChannelRequest(metricStarted, timedOut);
            }
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }
    }

    private static ZLinkFrameworkException CreateUnknownRouteTargetException(
        string routerChannelId,
        RoutingId targetNodeRid,
        string targetDescription,
        Exception? innerException = null)
    {
        return new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.RequestTargetNotFound,
            $"Route channel '{routerChannelId}' does not know node '{targetNodeRid}' for {targetDescription}.",
            innerException: innerException);
    }

    internal IZLinkBackendSocket GetMonitoringSocket(string sourceName)
    {
        return _channels.GetMonitoringSocket(GetOrStartState(), sourceName);
    }
}
