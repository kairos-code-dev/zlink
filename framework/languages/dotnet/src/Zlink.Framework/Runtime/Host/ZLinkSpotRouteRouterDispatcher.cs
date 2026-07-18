using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Spots;

namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkSpotRouteRouterDispatcher(
    Func<ZLinkFrameworkRuntimeState> getState)
{
    public ValueTask SendAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        ulong targetSpotGeneration,
        IReadOnlyList<Message> parts,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        return ResolveTarget(routerChannelId, targetNodeRid).SendAsync(
            targetNodeRid,
            targetSpotRid,
            targetSpotGeneration,
            parts,
            cancellationToken,
            metadata);
    }

    /// <summary>One-shot non-blocking spot send (TrySubmit surface). Only a
    /// local router-capable node supports it; the route-channel fallback still
    /// requires SubmitAsync (gap 90 §12.36 synchronous admission).</summary>
    public bool TrySendOnce(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        ulong targetSpotGeneration,
        IReadOnlyList<Message> parts,
        ReadOnlyMemory<byte> metadata = default)
    {
        var state = getState();
        ZLinkSpotOutboundTransport? outbound = null;
        if (state.TryGetSpotNodeByRoutingId(targetNodeRid, out var localSpotNode)
            && localSpotNode.Registration.Router is not null)
            outbound = localSpotNode.EntryOutbound;
        else if (state.SpotNodes.TryGetValue(routerChannelId, out var spotNodeRuntime)
                 && spotNodeRuntime.Registration.Router is not null)
            outbound = spotNodeRuntime.EntryOutbound;
        else
        {
            foreach (var candidate in state.SpotNodes.Values)
            {
                if (candidate.Registration.Router is null) continue;
                var meshName = candidate.Registration.SpotMeshChannelName
                               ?? candidate.Registration.SpotNodeName;
                if (!string.Equals(meshName, routerChannelId, StringComparison.Ordinal)) continue;
                outbound = candidate.EntryOutbound;
                break;
            }
        }

        if (outbound is null)
            throw ZLinkMeshCallSupport.TrySubmitPendingSyncAdmission();
        return outbound.TrySendToSpotOnce(
            targetNodeRid, targetSpotRid, targetSpotGeneration, parts, metadata);
    }

    public async ValueTask<IReadOnlyList<Message>> RequestAsync(
        string routerChannelId,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        ulong targetSpotGeneration,
        IReadOnlyList<Message> parts,
        TimeSpan timeout,
        CancellationToken cancellationToken,
        ReadOnlyMemory<byte> metadata = default)
    {
        return await ResolveTarget(routerChannelId, targetNodeRid).RequestAsync(
                targetNodeRid,
                targetSpotRid,
                targetSpotGeneration,
                parts,
                timeout,
                cancellationToken,
                metadata)
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
                localSpotNode.EntryOutbound);

        if (state.RouteChannels.TryGetValue(routerChannelId, out var routeChannel))
            return new RouteChannelTarget(routeChannel);

        if (state.SpotNodes.TryGetValue(routerChannelId, out var spotNodeRuntime))
            return new SpotNodeRouterTarget(
                spotNodeRuntime.EntryOutbound);

        throw new ZLinkConfigurationException(
            $"Router-capable channel '{routerChannelId}' is not registered in this process.");
    }

    private interface IRouterTarget
    {
        ValueTask SendAsync(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            ulong targetSpotGeneration,
            IReadOnlyList<Message> parts,
            CancellationToken cancellationToken,
            ReadOnlyMemory<byte> metadata);

        ValueTask<IReadOnlyList<Message>> RequestAsync(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            ulong targetSpotGeneration,
            IReadOnlyList<Message> parts,
            TimeSpan timeout,
            CancellationToken cancellationToken,
            ReadOnlyMemory<byte> metadata);
    }

    private sealed class SpotNodeRouterTarget(
        ZLinkSpotOutboundTransport outbound)
        : IRouterTarget
    {
        public ValueTask SendAsync(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            ulong targetSpotGeneration,
            IReadOnlyList<Message> parts,
            CancellationToken cancellationToken,
            ReadOnlyMemory<byte> metadata)
            => outbound.SendToSpotAsync(
                targetNodeRid,
                targetSpotRid,
                targetSpotGeneration,
                parts,
                cancellationToken,
                metadata);

        public async ValueTask<IReadOnlyList<Message>> RequestAsync(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            ulong targetSpotGeneration,
            IReadOnlyList<Message> parts,
            TimeSpan timeout,
            CancellationToken cancellationToken,
            ReadOnlyMemory<byte> metadata)
            => await outbound.RequestToSpotAsync(
                    targetNodeRid,
                    targetSpotRid,
                    targetSpotGeneration,
                    parts,
                    timeout,
                    cancellationToken,
                    metadata)
                .ConfigureAwait(false);
    }

    // RouteMesh 10.0.0: remote spot delivery no longer rides a ROUTER-socket
    // route bridge. Cross-node spot addressing now flows through the MeshNode spot
    // plane (ISpot.SendToSpot/RequestToSpot); routing a spot address to a bare
    // route channel is unsupported pending the S8 route/spot-plane follow-up.
    private sealed class RouteChannelTarget(ZLinkRouteChannelRuntime routeChannel)
        : IRouterTarget
    {
        public ValueTask SendAsync(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            ulong targetSpotGeneration,
            IReadOnlyList<Message> parts,
            CancellationToken cancellationToken,
            ReadOnlyMemory<byte> metadata)
        {
            throw new ZLinkConfigurationException(
                $"Route channel '{routeChannel.RouterChannelId}' cannot deliver spot-addressed " +
                "sends in RouteMesh 10.0.0; use the MeshNode spot plane.");
        }

        public ValueTask<IReadOnlyList<Message>> RequestAsync(
            RoutingId targetNodeRid,
            RoutingId targetSpotRid,
            ulong targetSpotGeneration,
            IReadOnlyList<Message> parts,
            TimeSpan timeout,
            CancellationToken cancellationToken,
            ReadOnlyMemory<byte> metadata)
        {
            throw new ZLinkConfigurationException(
                $"Route channel '{routeChannel.RouterChannelId}' cannot serve spot-addressed " +
                "requests in RouteMesh 10.0.0; use the MeshNode spot plane.");
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
