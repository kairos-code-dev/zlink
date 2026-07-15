namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    internal ZLinkChannelRuntimeBundle GetClientBundle(string channelName)
    {
        return _channels.GetClientBundle(GetOrStartState(), channelName);
    }

    internal ZLinkChannelRuntimeBundle GetPublisherBundle(string channelName)
    {
        return _channels.GetPublisherBundle(GetOrStartState(), channelName);
    }

    internal ZLinkRouteChannelRuntime GetRouteChannel(string routerChannelId)
    {
        return _channels.GetRouteChannel(GetOrStartState(), routerChannelId);
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
        return _topologyQuery?.IsKnownRouteMeshPeer(routerChannelId, targetNodeRid);
    }

    internal ValueTask SubmitRouteSendAsync<TMessage>(
        string routerChannelId,
        RoutingId targetNodeRid,
        string packetName,
        TMessage message,
        CancellationToken cancellationToken)
    {
        using var operation = EnterOperation();
        // Route channel sends target node rids only; spot-addressed
        // traffic goes through the address-based spot outbound instead
        // (spot-address messaging contract §6).
        var routeChannel = GetRouteChannel(routerChannelId);
        var known = IsKnownRouteMeshPeer(routerChannelId, targetNodeRid);
        if (known == false)
            throw CreateUnknownRouteTargetException(
                routerChannelId,
                targetNodeRid,
                $"packet '{packetName}'");

        return routeChannel.SubmitSendAsync(
            targetNodeRid,
            packetName,
            message,
            cancellationToken);
    }

    internal async ValueTask<TReply> SubmitRouteRequestAsync<TRequest, TReply>(
        string routerChannelId,
        RoutingId targetNodeRid,
        string packetName,
        TRequest request,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        using var operation = EnterOperation(countAsRequest: true);
        var known = IsKnownRouteMeshPeer(routerChannelId, targetNodeRid);
        if (known == false)
            throw CreateUnknownRouteTargetException(
                routerChannelId,
                targetNodeRid,
                $"packet '{packetName}'");

        return await GetRouteChannel(routerChannelId).RequestAsync<TRequest, TReply>(
                targetNodeRid,
                packetName,
                request,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal ValueTask SendToSpotViaRouterChannelAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        using var operation = EnterOperation();
        try
        {
            if (IsKnownRouteMeshPeer(routerChannelId, targetNodeRid) == false)
                throw CreateUnknownRouteTargetException(
                    routerChannelId,
                    targetNodeRid,
                    $"SPOT '{targetSpotRid}'");

            var accepted = _spotRouteRouter.SendAsync(
                routerChannelId,
                targetNodeRid,
                targetSpotRid,
                parts,
                cancellationToken);
            if (!accepted.IsCompletedSuccessfully)
                return AwaitAndDisposeSpotSendAsync(accepted, parts);

            accepted.GetAwaiter().GetResult();
            ZLinkMessageParts.DisposeAll(parts);
            return ValueTask.CompletedTask;
        }
        catch
        {
            ZLinkMessageParts.DisposeAll(parts);
            throw;
        }
    }

    private static async ValueTask AwaitAndDisposeSpotSendAsync(
        ValueTask accepted,
        IReadOnlyList<Message> parts)
    {
        try
        {
            await accepted.ConfigureAwait(false);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }
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
        try
        {
            using var operation = EnterOperation(countAsRequest: true);
            var metricStarted = ZLinkRuntimeMetrics.StartChannelRequest();
            var timedOut = false;
            try
            {
                if (IsKnownRouteMeshPeer(routerChannelId, targetNodeRid) == false)
                    throw CreateUnknownRouteTargetException(
                        routerChannelId,
                        targetNodeRid,
                        $"SPOT '{targetSpotRid}'");

                return await _spotRouteRouter.RequestAsync(
                        routerChannelId,
                        targetNodeRid,
                        targetSpotRid,
                        parts,
                        timeout,
                        cancellationToken)
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
