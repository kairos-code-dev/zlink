namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorEntrySpotJoinCoordinator(
    ZLinkFrameworkRegistration registration,
    ZLinkSpotRuntimeManager spots,
    ZLinkActorSessionManager actorSessionManager,
    Func<ZLinkFrameworkRuntimeState> getState,
    Func<IZLinkBackendSpotNode?> getActorSpotNode)
{
    public async ValueTask<ZLinkActorJoinResult> JoinAsync(
        RoutingId spotNodeRid,
        IZLinkActor actor,
        Message request,
        CancellationToken cancellationToken)
    {
        var actorState = actorSessionManager.GetOrCreateState(actor.ActorId);
        var node = getActorSpotNode()
            ?? throw new InvalidOperationException("Entry SPOT join requires a router-capable SpotNode.");
        var actorRef = actorState.NativeActorRef
            ?? throw new InvalidOperationException($"Actor '{actor.ActorId}' does not have a native Actor ref.");
        var previousActivation = actorState.LiveActivation;

        var tcs = new TaskCompletionSource<(ZLinkBackendActorJoinEntrySpotResult Result, IReadOnlyList<Message> Reply)>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        using var reg = cancellationToken.Register(
            static s => ((TaskCompletionSource<(ZLinkBackendActorJoinEntrySpotResult, IReadOnlyList<Message>)>)s!).TrySetCanceled(),
            tcs);

        if (!node.JoinActorEntrySpot(
                actorRef,
                spotNodeRid,
                request,
                (result, reply) => tcs.TrySetResult((result, reply)),
                registration.DefaultRequestTimeout))
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor entry SPOT join submit failed for '{actor.ActorId}'.");
        }

        var (result, replyParts) = await tcs.Task.ConfigureAwait(false);
        if (result.Result == RequestResult.NotConnected)
        {
            return await JoinRemoteAsync(
                    getState(),
                    spotNodeRid,
                    actor,
                    actorState,
                    actorRef,
                    previousActivation,
                    request,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        var reply = DecodeEntrySpotJoinReply(result.Result, replyParts, actor.ActorId, spotNodeRid);
        var accepted = result.JoinResultCode == 0;
        if (result.Result != RequestResult.Ok)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor entry SPOT join failed for '{actor.ActorId}' with '{result.Result}'.");
        }

        var resultActor = accepted ? result.Actor : actorRef;
        if (accepted)
        {
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
        }

        return new ZLinkActorJoinResult(
            Accepted: accepted,
            ToActorRef(resultActor),
            reply);
    }

    private async ValueTask<ZLinkActorJoinResult> JoinRemoteAsync(
        ZLinkFrameworkRuntimeState state,
        RoutingId spotNodeRid,
        IZLinkActor actor,
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef sourceActorRef,
        ZLinkSpotActivation? previousActivation,
        Message joinRequest,
        CancellationToken cancellationToken)
    {
        if (TryFindSpotNode(state, spotNodeRid, out var targetNode))
        {
            return await JoinLocalEntrySpotAsync(
                    targetNode,
                    actor,
                    actorState,
                    sourceActorRef,
                    previousActivation,
                    joinRequest,
                    cancellationToken)
                .ConfigureAwait(false);
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
            sourceActorRef.Generation,
            joinRequest.ToArray());

        var reply = await routeChannel.RequestAsync<ZLinkActorEntrySpotRouteJoinRequest, ZLinkActorEntrySpotRouteJoinReply>(
                spotNodeRid,
                ZLinkActorEntrySpotRoutePackets.JoinEntrySpotPacketName,
                request,
                registration.DefaultRequestTimeout,
                cancellationToken)
            .ConfigureAwait(false);

        var replyMessage = Message.From(reply.ReplyPayload);
        if (!reply.Accepted)
        {
            return new ZLinkActorJoinResult(
                Accepted: false,
                ToActorRef(sourceActorRef),
                replyMessage);
        }

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

        return new ZLinkActorJoinResult(
            Accepted: true,
            ToActorRef(targetRef),
            replyMessage);
    }

    private async ValueTask<ZLinkActorJoinResult> JoinLocalEntrySpotAsync(
        ZLinkSpotNodeRuntime targetNode,
        IZLinkActor actor,
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef sourceActorRef,
        ZLinkSpotActivation? previousActivation,
        Message joinRequest,
        CancellationToken cancellationToken)
    {
        var localTargetRef = targetNode.Node.ActorLookup(actor.ActorId)
            ?? targetNode.Node.CreateActor(actor.ActorId);
        var activation = targetNode.EntrySpotActivation
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor entry SPOT join target node '{localTargetRef.NodeRid}' does not have an Entry Spot activation.");
        var admission = activation.TryResolveActorJoin(out var descriptor) && descriptor is not null
            ? await activation.InvokeActorJoinAsync(descriptor, actor, joinRequest, cancellationToken)
                .ConfigureAwait(false)
            : ZLinkSpotActorJoinResult.Reject();
        var reply = CopyReply(admission.Reply);
        if (!admission.Accepted)
        {
            return new ZLinkActorJoinResult(
                Accepted: false,
                ToActorRef(sourceActorRef),
                reply);
        }

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

        return new ZLinkActorJoinResult(
            Accepted: true,
            ToActorRef(localTargetRef),
            reply);
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
                cancellationToken)
            .ConfigureAwait(false);
    }

    private static ActorRef ToActorRef(ZLinkBackendActorRef actorRef)
        => new(actorRef.NodeRid, actorRef.ActorId, actorRef.Generation);

    private static Message CopyReply(Message? reply)
    {
        return reply is null
            ? Message.From(ReadOnlySpan<byte>.Empty)
            : Message.From(reply);
    }

    private static Message DecodeEntrySpotJoinReply(
        RequestResult result,
        IReadOnlyList<Message> replyParts,
        string actorId,
        RoutingId spotNodeRid)
    {
        try
        {
            if (result != RequestResult.Ok)
            {
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"Actor entry SPOT join was rejected for '{actorId}' to node '{spotNodeRid}'.");
            }

            if (replyParts.Count == 0)
            {
                return Message.From(ReadOnlySpan<byte>.Empty);
            }

            return Message.From(replyParts[0]);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(replyParts);
        }
    }
}
