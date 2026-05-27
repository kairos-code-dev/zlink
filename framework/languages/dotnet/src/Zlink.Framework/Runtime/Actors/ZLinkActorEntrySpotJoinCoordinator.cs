namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorEntrySpotJoinCoordinator(
    ZLinkFrameworkRegistration registration,
    ZLinkSpotRuntimeManager spots,
    ZLinkActorSessionManager actorSessionManager,
    Func<ZLinkFrameworkRuntimeState> getState,
    Func<IZLinkBackendSpotNode?> getActorSpotNode)
{
    public async ValueTask<ActorRef> JoinAsync(
        RoutingId spotNodeRid,
        IZLinkActor actor,
        CancellationToken cancellationToken)
    {
        var actorState = actorSessionManager.GetOrCreateState(actor.ActorId);
        var node = getActorSpotNode()
            ?? throw new InvalidOperationException("Entry SPOT join requires a router-capable SpotNode.");
        var actorRef = actorState.NativeActorRef
            ?? throw new InvalidOperationException($"Actor '{actor.ActorId}' does not have a native Actor ref.");
        var previousActivation = actorState.LiveActivation;

        var tcs = new TaskCompletionSource<ZLinkBackendActorJoinEntrySpotResult>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        using var reg = cancellationToken.Register(
            static s => ((TaskCompletionSource<ZLinkBackendActorJoinEntrySpotResult>)s!).TrySetCanceled(),
            tcs);

        if (!node.JoinActorEntrySpot(
                actorRef,
                spotNodeRid,
                result => tcs.TrySetResult(result),
                registration.DefaultTimeout))
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor entry SPOT join submit failed for '{actor.ActorId}'.");
        }

        var result = await tcs.Task.ConfigureAwait(false);
        if (result.Result == RequestResult.NotConnected)
        {
            return await JoinRemoteAsync(
                    getState(),
                    spotNodeRid,
                    actor,
                    actorState,
                    actorRef,
                    previousActivation,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        if (result.Result != RequestResult.Ok)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor entry SPOT join failed for '{actor.ActorId}' with '{result.Result}'.");
        }

        actorState.NativeActorRef = result.Actor;
        await NotifyManagedEntrySpotJoinLifecycleAsync(
                actor,
                previousActivation,
                result.Actor.NodeRid,
                cancellationToken)
            .ConfigureAwait(false);
        if (result.Actor.NodeRid != actorRef.NodeRid)
        {
            actorState.InvalidateContext();
        }

        return ToActorRef(result.Actor);
    }

    private async ValueTask<ActorRef> JoinRemoteAsync(
        ZLinkFrameworkRuntimeState state,
        RoutingId spotNodeRid,
        IZLinkActor actor,
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef sourceActorRef,
        ZLinkSpotActivation? previousActivation,
        CancellationToken cancellationToken)
    {
        if (TryFindSpotNode(state, spotNodeRid, out var targetNode))
        {
            var localTargetRef = targetNode.Node.ActorLookup(actor.ActorId)
                ?? targetNode.Node.CreateActor(actor.ActorId);
            actorState.NativeActorRef = localTargetRef;
            await NotifyManagedEntrySpotJoinLifecycleAsync(
                    actor,
                    previousActivation,
                    localTargetRef.NodeRid,
                    cancellationToken)
                .ConfigureAwait(false);
            if (localTargetRef.NodeRid != sourceActorRef.NodeRid)
            {
                actorState.InvalidateContext();
            }

            return ToActorRef(localTargetRef);
        }

        var routeChannel = state.RouteChannels.Values.FirstOrDefault()
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor entry SPOT join failed for '{actor.ActorId}' because target node '{spotNodeRid}' is not connected and no route channel is registered.");

        var request = new ZLinkActorEntrySpotRouteJoinRequest(
            actor.ActorId,
            actorState.ActorType ?? actor.GetType().Name,
            sourceActorRef.NodeRid.ToHex(),
            previousActivation?.SpotRid.ToHex() ?? string.Empty,
            sourceActorRef.Generation);

        var reply = await routeChannel.RequestAsync<ZLinkActorEntrySpotRouteJoinRequest, ZLinkActorEntrySpotRouteJoinReply>(
                spotNodeRid,
                ZLinkActorEntrySpotRoutePackets.JoinEntrySpotPacketName,
                request,
                registration.DefaultTimeout,
                cancellationToken)
            .ConfigureAwait(false);

        var targetRef = new ZLinkBackendActorRef(
            RoutingId.From(reply.TargetNodeRid),
            actor.ActorId,
            reply.ActorGeneration);
        actorState.NativeActorRef = targetRef;
        await NotifyManagedUserSpotLeftForEntrySpotJoinAsync(
                actor,
                previousActivation,
                cancellationToken)
            .ConfigureAwait(false);
        if (targetRef.NodeRid != sourceActorRef.NodeRid)
        {
            actorState.InvalidateContext();
        }

        return ToActorRef(targetRef);
    }

    private static bool TryFindSpotNode(
        ZLinkFrameworkRuntimeState state,
        RoutingId nodeRid,
        out ZLinkSpotNodeRuntime nodeRuntime)
    {
        foreach (var candidate in state.SpotNodes.Values)
        {
            if (candidate.Node.RoutingId == nodeRid)
            {
                nodeRuntime = candidate;
                return true;
            }
        }

        nodeRuntime = null!;
        return false;
    }

    private async ValueTask NotifyManagedEntrySpotJoinLifecycleAsync(
        IZLinkActor actor,
        ZLinkSpotActivation? previousActivation,
        RoutingId targetNodeRid,
        CancellationToken cancellationToken)
    {
        if (previousActivation is null)
        {
            return;
        }

        await NotifyManagedUserSpotLeftForEntrySpotJoinAsync(
                actor,
                previousActivation,
                cancellationToken)
            .ConfigureAwait(false);

        await spots.NotifyEntrySpotActorJoinedAsync(
                getState(),
                actor,
                new ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind.JoinEntrySpot),
                targetNodeRid,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private static async ValueTask NotifyManagedUserSpotLeftForEntrySpotJoinAsync(
        IZLinkActor actor,
        ZLinkSpotActivation? previousActivation,
        CancellationToken cancellationToken)
    {
        if (previousActivation is null)
        {
            return;
        }

        await previousActivation.NotifyActorLeftAfterNativeJoinEntrySpotAsync(
                actor,
                new ZLinkSpotActorChangeResult(ZLinkSpotActorChangeKind.JoinEntrySpot),
                cancellationToken)
            .ConfigureAwait(false);
    }

    private static ActorRef ToActorRef(ZLinkBackendActorRef actorRef)
        => new(actorRef.NodeRid, actorRef.ActorId, actorRef.Generation);
}
