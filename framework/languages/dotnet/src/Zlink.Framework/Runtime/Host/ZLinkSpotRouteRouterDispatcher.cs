namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkSpotRouteRouterDispatcher(
    Func<ZLinkFrameworkRuntimeState> getState)
{
    public async ValueTask SendAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken)
    {
        await ResolveTarget(routerChannelId, targetNodeRid).SendAsync(
                targetNodeRid,
                targetSpotRid,
                parts,
                cancellationToken)
            .ConfigureAwait(false);
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

    private static bool TryResolveLocalAcceptedSpotNode(
        ZLinkFrameworkRuntimeState state,
        string routerChannelId,
        RoutingId targetNodeRid,
        out ZLinkSpotNodeRuntime spotNodeRuntime)
    {
        var candidate = ResolveRouterSpotNodeByRoutingId(state, targetNodeRid);
        if (candidate is not null)
        {
            spotNodeRuntime = candidate;
            return true;
        }

        spotNodeRuntime = null!;
        return false;
    }

    private static ZLinkSpotNodeRuntime? ResolveRouterSpotNodeByRoutingId(
        ZLinkFrameworkRuntimeState state,
        RoutingId targetNodeRid)
    {
        foreach (var candidate in state.SpotNodes.Values)
            if (candidate.Registration.Router is not null
                && candidate.Node.RoutingId == targetNodeRid)
                return candidate;

        return null;
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
        string routerChannelId,
        RoutingId? targetNodeRid = null)
    {
        if (targetNodeRid is { } rid)
            foreach (var candidate in state.SpotNodes.Values)
                if (candidate.Registration.Router is not null
                    && candidate.Node.RoutingId == rid)
                    return candidate;

        if (state.SpotNodes.TryGetValue(routerChannelId, out var named)
            && named.Registration.Router is not null)
            return named;

        return ResolveSingleRouterSpotNode(state);
    }

    private IRouterTarget ResolveTarget(string routerChannelId, RoutingId targetNodeRid)
    {
        var state = getState();
        if (TryResolveLocalAcceptedSpotNode(
                state,
                routerChannelId,
                targetNodeRid,
                out var localSpotNode))
            return new SpotNodeRouterTarget(
                routerChannelId,
                localSpotNode.Node.EntrySpot(),
                "Local SPOT node");

        if (state.RouteChannels.TryGetValue(routerChannelId, out var routeChannel))
            return new RouteChannelTarget(routeChannel);

        if (state.ServerBundles.TryGetValue(routerChannelId, out var serverBundle)
            && serverBundle.Socket is IZLinkBackendRouterSocket)
            return new ServerRouterTarget(
                routerChannelId,
                serverBundle.SpotRouteBridge
                ?? throw new ZLinkConfigurationException(
                    $"Router channel '{routerChannelId}' is not attached to a SPOT route bridge."),
                serverBundle.ReceiveGate);

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
            try
            {
                if (!entrySpot.SendToSpot(
                        targetNodeRid,
                        targetSpotRid,
                        parts,
                        SendFlags.None))
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"{sourceLabel} for route channel '{routerChannelId}' is not ready for SPOT send.");
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(parts);
            }

            return ValueTask.CompletedTask;
        }

        public async ValueTask<IReadOnlyList<Message>> RequestAsync(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            TimeSpan timeout,
            CancellationToken cancellationToken)
        {
            var completion = new TaskCompletionSource<IReadOnlyList<Message>>(
                TaskCreationOptions.RunContinuationsAsynchronously);

            try
            {
                if (!entrySpot.RequestToSpot(
                        targetNodeRid,
                        targetSpotRid,
                        parts,
                        (result, reply) => ZLinkRawReplyCompletion.Complete(
                            result,
                            reply,
                            completion,
                            $"SpotNode router '{routerChannelId}' SPOT request failed with result '{result}'."),
                        SendFlags.None,
                        timeout))
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"SpotNode router '{routerChannelId}' is not ready for SPOT request.");
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(parts);
            }

            using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeoutSource.CancelAfter(timeout);
            using var _ = timeoutSource.Token.Register(
                static state => ((TaskCompletionSource<IReadOnlyList<Message>>)state!).TrySetCanceled(),
                completion);
            return await completion.Task.ConfigureAwait(false);
        }
    }

    private sealed class RouteChannelTarget(ZLinkRouteChannelRuntime routeChannel)
        : IRouterTarget
    {
        // Remote spot delivery over a route channel uses the bridge relay
        // framing exclusively: it is the only spot inbound the receiving
        // pump dispatches (spot-address messaging draft §6).
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

            ZLinkMessageParts.DisposeAll(parts);
            return ValueTask.CompletedTask;
        }

        public async ValueTask<IReadOnlyList<Message>> RequestAsync(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            TimeSpan timeout,
            CancellationToken cancellationToken)
        {
            var completion = new TaskCompletionSource<IReadOnlyList<Message>>(
                TaskCreationOptions.RunContinuationsAsynchronously);
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
                            ZLinkMessageParts.DisposeAll(reply);
                            completion.TrySetException(new ZLinkFrameworkException(
                                ZLinkFrameworkErrorKind.SpotRouteNotFound,
                                $"SPOT '{targetSpotRid}' does not live on node '{targetNodeRid}'."));
                            return;
                        }

                        ZLinkRawReplyCompletion.Complete(
                            result,
                            reply,
                            completion,
                            $"SPOT route bridge request failed with result '{result}'.");
                    },
                    timeout))
                throw new ZLinkConfigurationException(
                    $"Route channel '{routeChannel.RouterChannelId}' has no SPOT route bridge for remote spot requests.");

            ZLinkMessageParts.DisposeAll(parts);
            using var timeoutSource = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
            timeoutSource.CancelAfter(timeout);
            using var _ = timeoutSource.Token.Register(
                static state => ((TaskCompletionSource<IReadOnlyList<Message>>)state!).TrySetCanceled(),
                completion);
            try
            {
                return await completion.Task.ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
            {
                throw new TimeoutException(
                    $"SPOT route bridge request to '{targetSpotRid}' timed out.");
            }
        }
    }

    private sealed class ServerRouterTarget(
        string routerChannelId,
        IZLinkBackendSpotRouteBridge bridge,
        SemaphoreSlim receiveGate)
        : IRouterTarget
    {
        public ValueTask SendAsync(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            CancellationToken cancellationToken)
        {
            try
            {
                if (!bridge.Send(
                        routerChannelId,
                        targetNodeRid,
                        targetSpotRid,
                        parts,
                        SendFlags.None))
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"Router channel '{routerChannelId}' is not ready for SPOT send.");
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(parts);
            }

            return ValueTask.CompletedTask;
        }

        public async ValueTask<IReadOnlyList<Message>> RequestAsync(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            IReadOnlyList<Message> parts,
            TimeSpan timeout,
            CancellationToken cancellationToken)
        {
            var completion = new TaskCompletionSource<IReadOnlyList<Message>>(
                TaskCreationOptions.RunContinuationsAsynchronously);

            await receiveGate.WaitAsync(cancellationToken).ConfigureAwait(false);
            try
            {
                try
                {
                    if (!bridge.Request(
                            routerChannelId,
                            targetNodeRid,
                            targetSpotRid,
                            parts,
                            (result, reply) => ZLinkRawReplyCompletion.Complete(
                                result,
                                reply,
                                completion,
                                $"Router channel '{routerChannelId}' SPOT request failed with result '{result}'."),
                            SendFlags.None,
                            timeout))
                        throw new ZLinkFrameworkException(
                            ZLinkFrameworkErrorKind.ActorRouteNotFound,
                            $"Router channel '{routerChannelId}' is not ready for SPOT request.");
                }
                finally
                {
                    ZLinkMessageParts.DisposeAll(parts);
                }

                using var _ = cancellationToken.Register(
                    static state => ((TaskCompletionSource<IReadOnlyList<Message>>)state!)
                        .TrySetCanceled(),
                    completion);

                return await completion.Task.ConfigureAwait(false);
            }
            finally
            {
                receiveGate.Release();
            }
        }
    }
}
