namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkSpotRouteRouterDispatcher(
    Func<ZLinkFrameworkRuntimeState> getState)
{
    public ValueTask SendAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        return ResolveTarget(routerChannelId, targetNodeRid).SendAsync(
            targetNodeRid,
            targetSpotRid,
            parts,
            cancellationToken);
    }

    public async ValueTask<IReadOnlyList<Message>> RequestAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return await ResolveTarget(routerChannelId, targetNodeRid).RequestAsync(
                targetNodeRid,
                targetSpotRid,
                parts,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public RoutingId ResolveAcceptedSpotRouteNodeRid(string targetSpotNodeChannelName)
    {
        var state = getState();
        return ResolveRouterSpotNode(state, targetSpotNodeChannelName)?.Node.RoutingId
               ?? throw new ZLinkConfigurationException(
                   $"Routed SPOT target channel '{targetSpotNodeChannelName}' is not owned by a router-capable SPOT node in this process.");
    }

    private static ZLinkSpotNodeRuntime? ResolveSingleRouterSpotNode(
        ZLinkFrameworkRuntimeState state)
    {
        ZLinkSpotNodeRuntime? matched = null;
        foreach (var candidate in state.SpotNodes.Values)
        {
            if (candidate.Registration.Router is null) continue;

            if (matched is not null)
                throw new ZLinkConfigurationException(
                    "Routed SPOT dispatch requires exactly one router-capable SPOT node in this process.");

            matched = candidate;
        }

        return matched;
    }

    private static ZLinkSpotNodeRuntime? ResolveRouterSpotNode(
        ZLinkFrameworkRuntimeState state,
        string routerChannelId)
    {
        if (state.SpotNodes.TryGetValue(routerChannelId, out var named)
            && named.Registration.Router is not null)
            return named;

        return ResolveSingleRouterSpotNode(state);
    }

    private IRouterTarget ResolveTarget(string routerChannelId, RoutingId targetNodeRid)
    {
        var state = getState();
        if (state.TryGetSpotNodeByRoutingId(targetNodeRid, out var localSpotNode)
            && localSpotNode.Registration.Router is not null)
            return new SpotNodeRouterTarget(
                routerChannelId,
                localSpotNode.Node.EntrySpot(),
                "Local SPOT node");

        if (state.RouteChannels.TryGetValue(routerChannelId, out var routeChannel))
            return new RouteChannelTarget(routeChannel);

        if (state.SpotNodes.TryGetValue(routerChannelId, out var spotNodeRuntime))
            return new SpotNodeRouterTarget(
                routerChannelId,
                spotNodeRuntime.Node.EntrySpot(),
                "SpotNode router");

        throw new ZLinkConfigurationException(
            $"Router-capable channel '{routerChannelId}' is not registered in this process.");
    }

    private interface IRouterTarget
    {
        ValueTask SendAsync(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            CancellationToken cancellationToken);

        ValueTask<IReadOnlyList<Message>> RequestAsync(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            TimeSpan timeout,
            CancellationToken cancellationToken);
    }

    private sealed class SpotNodeRouterTarget(
        string routerChannelId,
        IZLinkBackendSpot entrySpot,
        string sourceLabel)
        : IRouterTarget
    {
        public ValueTask SendAsync(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            CancellationToken cancellationToken)
        {
            if (!entrySpot.SendToSpot(
                    targetNodeRid,
                    targetSpotRid,
                    parts,
                    SendFlags.DontWait))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"{sourceLabel} for route channel '{routerChannelId}' is not ready for SPOT send.");

            return ValueTask.CompletedTask;
        }

        public async ValueTask<IReadOnlyList<Message>> RequestAsync(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            TimeSpan timeout,
            CancellationToken cancellationToken)
        {
            using var completion = new ZLinkNativeReplyCompletion<RequestResult>(
                cancellationToken,
                timeout,
                "SPOT node router request timed out.");
            if (!entrySpot.RequestToSpot(
                    targetNodeRid,
                    targetSpotRid,
                    parts,
                    (result, reply) => CompleteRouteRequest(
                        completion,
                        result,
                        reply,
                        $"SpotNode router '{routerChannelId}' SPOT request failed with result '{result}'."),
                    SendFlags.None,
                    timeout))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"SpotNode router '{routerChannelId}' is not ready for SPOT request.");

            var (_, reply) = await completion.Task.ConfigureAwait(false);
            return reply;
        }
    }

    private sealed class RouteChannelTarget(ZLinkRouteChannelRuntime routeChannel)
        : IRouterTarget
    {
        // Remote spot delivery over a route channel uses the bridge relay
        // framing exclusively: it is the only spot inbound the receiving
        // pump dispatches (spot-address messaging contract §6).
        public ValueTask SendAsync(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            CancellationToken cancellationToken)
        {
            if (!routeChannel.TrySendViaSpotRouteBridge(
                    targetNodeRid,
                    targetSpotRid,
                    parts))
                throw new ZLinkConfigurationException(
                    $"Route channel '{routeChannel.RouterChannelId}' has no SPOT route bridge for remote spot delivery.");

            return ValueTask.CompletedTask;
        }

        public async ValueTask<IReadOnlyList<Message>> RequestAsync(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            TimeSpan timeout,
            CancellationToken cancellationToken)
        {
            using var completion = new ZLinkNativeReplyCompletion<RequestResult>(
                cancellationToken,
                timeout,
                $"SPOT route bridge request to '{targetSpotRid}' timed out.");
            if (!routeChannel.TryRequestViaSpotRouteBridge(
                        targetNodeRid,
                        targetSpotRid,
                        parts,
                        (result, reply) =>
                        {
                            // NotFound from a spot-addressed request means the
                            // target node answered "no such spot here": a stale
                            // spot address, not an unknown node.
                            if (result == RequestResult.NotFound)
                            {
                                completion.Fail(
                                    new ZLinkFrameworkException(
                                        ZLinkFrameworkErrorKind.SpotRouteNotFound,
                                        $"SPOT '{targetSpotRid}' does not live on node '{targetNodeRid}'."),
                                    reply);
                                return;
                            }

                            CompleteRouteRequest(
                                completion,
                                result,
                                reply,
                                $"SPOT route bridge request failed with result '{result}'.");
                        },
                        timeout))
                throw new ZLinkConfigurationException(
                    $"Route channel '{routeChannel.RouterChannelId}' has no SPOT route bridge for remote spot requests.");

            var (_, reply) = await completion.Task.ConfigureAwait(false);
            return reply;
        }
    }

    private static void CompleteRouteRequest(
        ZLinkNativeReplyCompletion<RequestResult> completion,
        RequestResult result,
        IReadOnlyList<Message> reply,
        string failureMessage)
    {
        if (result == RequestResult.Ok)
        {
            completion.Complete(result, reply);
            return;
        }

        completion.Fail(
            ZLinkRequestFailureMapper.CreateCompletionException(result, failureMessage),
            reply);
    }

}
