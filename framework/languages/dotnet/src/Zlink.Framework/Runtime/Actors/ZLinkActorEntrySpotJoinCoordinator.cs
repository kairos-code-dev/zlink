namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorEntrySpotJoinCoordinator(
    ZLinkFrameworkRegistration registration,
    ZLinkSpotRuntimeManager spots,
    ZLinkActorSessionManager actorSessionManager,
    Func<ZLinkFrameworkRuntimeState> getState,
    Func<IZLinkBackendSpotNode?> getActorSpotNode,
    ZLinkMessageFlowTracer flow)
{
    public async ValueTask<ZLinkActorJoinResult> JoinAsync(
        RoutingId spotNodeRid,
        IZLinkActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var actorState = actorSessionManager.GetOrCreateState(actor.ActorId);
        var node = getActorSpotNode()
                   ?? throw new InvalidOperationException("Entry SPOT join requires a router-capable SpotNode.");
        var actorRef = actorState.NativeActorRef
                       ?? throw new InvalidOperationException(
                           $"Actor '{actor.ActorId}' does not have a native Actor ref.");
        var previousActivation = actorState.LiveActivation;

        var tcs = new TaskCompletionSource<(ZLinkBackendActorJoinEntrySpotResult Result, IReadOnlyList<Message> Reply)>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        using var reg = cancellationToken.Register(
            static s => ((TaskCompletionSource<(ZLinkBackendActorJoinEntrySpotResult, IReadOnlyList<Message>)>)s!)
                .TrySetCanceled(),
            tcs);

        var correlationId = Guid.NewGuid().ToString("N");
        if (flow.Enabled(ZLinkMessageFlowOutcome.Sent))
            flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.Sent,
                ZLinkDispatchErrorSurface.SpotActor,
                ZLinkDispatchMessageKind.ActorRequest,
                "JoinEntrySpot",
                CorrelationId: correlationId,
                SourceRid: spotNodeRid.ToString(),
                ActorId: actor.ActorId));

        var encodedRequest = request.Encode(registration.Codecs);
        using (var nativeRequest = ZLinkEnvelopeCodec.EncodePart(new ZLinkActorJoinSinglePartEnvelope(
                   encodedRequest.ContentType,
                   encodedRequest.Payload.ToArray())))
        {
            if (!node.JoinActorEntrySpot(
                    actorRef,
                    spotNodeRid,
                    nativeRequest,
                    (result, reply) => tcs.TrySetResult((result, reply)),
                    registration.DefaultRequestTimeout))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"Actor entry SPOT join submit failed for '{actor.ActorId}'.");
        }

        var (result, replyParts) = await tcs.Task.ConfigureAwait(false);
        if (result.Result == RequestResult.NotConnected)
            // Not connected locally — the remote fallback below is traced by the
            // route client; no reply_received here.
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

        if (flow.Enabled(ZLinkMessageFlowOutcome.ReplyReceived))
            flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.ReplyReceived,
                ZLinkDispatchErrorSurface.SpotActor,
                ZLinkDispatchMessageKind.Response,
                "JoinEntrySpot",
                CorrelationId: correlationId,
                SourceRid: spotNodeRid.ToString(),
                ActorId: actor.ActorId));

        var reply = DecodeEntrySpotJoinReply(result.Result, replyParts, actor.ActorId, spotNodeRid);
        var accepted = result.JoinResultCode == 0;
        if (result.Result != RequestResult.Ok)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor entry SPOT join failed for '{actor.ActorId}' with '{result.Result}'.");

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
            if (result.Actor.NodeRid != actorRef.NodeRid) actorState.InvalidateContext();
        }

        return new ZLinkActorJoinResult(
            accepted,
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
        ZLinkMessage joinRequest,
        CancellationToken cancellationToken)
    {
        if (TryFindSpotNode(state, spotNodeRid, out var targetNode))
            return await JoinLocalEntrySpotAsync(
                    targetNode,
                    actor,
                    actorState,
                    sourceActorRef,
                    previousActivation,
                    joinRequest,
                    cancellationToken)
                .ConfigureAwait(false);

        var routeChannel = state.RouteChannels.Values.FirstOrDefault()
                           ?? throw new ZLinkFrameworkException(
                               ZLinkFrameworkErrorKind.ActorRouteNotFound,
                               $"Actor entry SPOT join failed for '{actor.ActorId}' because target node '{spotNodeRid}' is not connected and no route channel is registered.");

        var encodedRequest = joinRequest.Encode(registration.Codecs);
        ZLinkActorEntrySpotRouteJoinRequest request;
        request = new ZLinkActorEntrySpotRouteJoinRequest(
            actor.ActorId,
            actorState.ActorType ?? actor.GetType().Name,
            sourceActorRef.NodeRid.ToHex(),
            previousActivation?.SpotRid.ToHex() ?? string.Empty,
            sourceActorRef.Generation,
            encodedRequest.ContentType,
            encodedRequest.Payload.ToArray());

        var reply = await routeChannel
            .RequestAsync<ZLinkActorEntrySpotRouteJoinRequest, ZLinkActorEntrySpotRouteJoinReply>(
                spotNodeRid,
                ZLinkActorEntrySpotRoutePackets.JoinEntrySpotPacketName,
                request,
                registration.DefaultRequestTimeout,
                cancellationToken)
            .ConfigureAwait(false);

        using var replyPayload = Message.From(reply.ReplyPayload);
        var replyMessage = ZLinkMessage.FromEnvelopePayload(
            reply.ReplyContentType,
            replyPayload,
            registration.Codecs);
        if (!reply.Accepted)
            return new ZLinkActorJoinResult(
                false,
                ToActorRef(sourceActorRef),
                replyMessage);

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
        if (targetRef.NodeRid != sourceActorRef.NodeRid) actorState.InvalidateContext();

        return new ZLinkActorJoinResult(
            true,
            ToActorRef(targetRef),
            replyMessage);
    }

    private async ValueTask<ZLinkActorJoinResult> JoinLocalEntrySpotAsync(
        ZLinkSpotNodeRuntime targetNode,
        IZLinkActor actor,
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef sourceActorRef,
        ZLinkSpotActivation? previousActivation,
        ZLinkMessage joinRequest,
        CancellationToken cancellationToken)
    {
        var localTargetRef = targetNode.Node.ActorLookup(actor.ActorId);
        ZLinkBackendActorRef targetRef;
        if (localTargetRef is { } existing)
        {
            targetRef = existing;
        }
        else
        {
            using var emptyCreateRequest = Message.From(ReadOnlySpan<byte>.Empty);
            targetRef = targetNode.Node.CreateActor(actor.ActorId, emptyCreateRequest);
        }

        var activation = targetNode.EntrySpotActivation
                         ?? throw new ZLinkFrameworkException(
                             ZLinkFrameworkErrorKind.ActorRouteNotFound,
                             $"Actor entry SPOT join target node '{targetRef.NodeRid}' does not have an Entry Spot activation.");
        var admission = activation.TryResolveActorJoin(out var descriptor) && descriptor is not null
            ? await activation.InvokeActorJoinAsync(descriptor, actor, joinRequest, cancellationToken)
                .ConfigureAwait(false)
            : ZLinkSpotActorJoinResult.Reject();
        var reply = CopyReply(admission.Reply);
        if (!admission.Accepted)
            return new ZLinkActorJoinResult(
                false,
                ToActorRef(sourceActorRef),
                reply);

        actorState.NativeActorRef = targetRef;
        await NotifyManagedEntrySpotJoinLifecycleAsync(
                actor,
                previousActivation,
                targetRef.NodeRid,
                cancellationToken)
            .ConfigureAwait(false);
        if (targetRef.NodeRid != sourceActorRef.NodeRid) actorState.InvalidateContext();

        return new ZLinkActorJoinResult(
            true,
            ToActorRef(targetRef),
            reply);
    }

    private static bool TryFindSpotNode(
        ZLinkFrameworkRuntimeState state,
        RoutingId nodeRid,
        out ZLinkSpotNodeRuntime nodeRuntime)
    {
        foreach (var candidate in state.SpotNodes.Values)
            if (candidate.Node.RoutingId == nodeRid)
            {
                nodeRuntime = candidate;
                return true;
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
        if (previousActivation is null) return;

        await previousActivation.NotifyActorLeftAfterNativeJoinEntrySpotAsync(
                actor,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private static ActorRef ToActorRef(ZLinkBackendActorRef actorRef)
    {
        return new ActorRef(actorRef.NodeRid, actorRef.ActorId, actorRef.Generation);
    }

    private static ZLinkMessage CopyReply(ZLinkMessage? reply)
    {
        return reply ?? ZLinkMessage.Empty;
    }

    private ZLinkMessage DecodeEntrySpotJoinReply(
        RequestResult result,
        IReadOnlyList<Message> replyParts,
        string actorId,
        RoutingId spotNodeRid)
    {
        try
        {
            if (result != RequestResult.Ok)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"Actor entry SPOT join was rejected for '{actorId}' to node '{spotNodeRid}'.");

            if (replyParts.Count == 0) return ZLinkMessage.Empty;

            if (replyParts.Count == 1)
                return ZLinkMessage.FromEnvelopePayload(
                    ZLinkEnvelopeCodec.DefaultContentType,
                    replyParts[0],
                    registration.Codecs);

            var header = ZLinkEnvelopeCodec.DecodeHeader(replyParts);
            return ZLinkMessage.FromEnvelopePayload(header.ContentType, replyParts[1], registration.Codecs);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(replyParts);
        }
    }
}