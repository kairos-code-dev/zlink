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
            using var completion = new ZLinkSpotRouteRequestCompletion(
                timeout,
                cancellationToken,
                "SPOT node router request timed out.");
            try
            {
                if (!entrySpot.RequestToSpot(
                        targetNodeRid,
                        targetSpotRid,
                        parts,
                        (result, reply) => completion.Complete(
                            result,
                            reply,
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

            return await completion.WaitAsync().ConfigureAwait(false);
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
            using var completion = new ZLinkSpotRouteRequestCompletion(
                timeout,
                cancellationToken,
                $"SPOT route bridge request to '{targetSpotRid}' timed out.");
            try
            {
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

                            completion.Complete(
                                result,
                                reply,
                                $"SPOT route bridge request failed with result '{result}'.");
                        },
                        timeout))
                    throw new ZLinkConfigurationException(
                        $"Route channel '{routeChannel.RouterChannelId}' has no SPOT route bridge for remote spot requests.");
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(parts);
            }

            return await completion.WaitAsync().ConfigureAwait(false);
        }
    }

    private sealed class ZLinkSpotRouteRequestCompletion : IDisposable
    {
        private readonly TaskCompletionSource<IReadOnlyList<Message>> _completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        private readonly CancellationTokenRegistration _cancellationRegistration;
        private readonly CancellationTokenSource _timeoutSource = new();
        private readonly CancellationTokenRegistration _timeoutRegistration;

        public ZLinkSpotRouteRequestCompletion(
            TimeSpan timeout,
            CancellationToken cancellationToken,
            string timeoutMessage)
        {
            _timeoutSource.CancelAfter(timeout);
            _timeoutRegistration = _timeoutSource.Token.Register(
                static state =>
                {
                    var (completion, message) =
                        ((TaskCompletionSource<IReadOnlyList<Message>>, string))state!;
                    completion.TrySetException(new TimeoutException(message));
                },
                (_completion, timeoutMessage));
            _cancellationRegistration = cancellationToken.Register(
                static state =>
                {
                    var (completion, token) =
                        ((TaskCompletionSource<IReadOnlyList<Message>>, CancellationToken))state!;
                    completion.TrySetCanceled(token);
                },
                (_completion, cancellationToken));
        }

        public void Complete(
            RequestResult result,
            IReadOnlyList<Message> reply,
            string failureMessageFormat)
        {
            ZLinkRawReplyCompletion.Complete(
                result,
                reply,
                _completion,
                failureMessageFormat);
        }

        public void Fail(
            Exception exception,
            IReadOnlyList<Message> reply)
        {
            ZLinkMessageParts.DisposeAll(reply);
            _completion.TrySetException(exception);
        }

        public async ValueTask<IReadOnlyList<Message>> WaitAsync()
        {
            return await _completion.Task.ConfigureAwait(false);
        }

        public void Dispose()
        {
            _cancellationRegistration.Dispose();
            _timeoutRegistration.Dispose();
            _timeoutSource.Dispose();
        }
    }

}
