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
        {
            ZLinkMessageParts.DisposeAll(replyParts);
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
        }

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

        if (accepted)
        {
            actorState.BindNativeActorRef(result.Actor);
            await NotifyManagedEntrySpotJoinLifecycleAsync(
                    actor,
                    previousActivation,
                    result.Actor.NodeRid,
                    cancellationToken)
                .ConfigureAwait(false);
            if (result.Actor.NodeRid != actorRef.NodeRid)
            {
                actorState.InvalidateContext();
                // Native entry-spot join: no framework runtime claims the
                // row on the target, so this owner renews it with the new
                // node rid instead of releasing it.
                await actorSessionManager.RenewActorLocationAfterEntrySpotMoveAsync(
                        actorState,
                        result.Actor.NodeRid,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
        }

        return new ZLinkActorJoinResult(
            accepted,
            accepted ? result.Actor.ToNative() : null,
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
        if (state.TryGetSpotNodeByRoutingId(spotNodeRid, out var targetNode))
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

        var request = ZLinkActorEntrySpotRoutePackets.CreateJoinRequest(
            actor.ActorId,
            actorState.ActorType ?? actor.GetType().Name,
            sourceActorRef,
            previousActivation?.SpotRid,
            joinRequest,
            registration.Codecs);

        var reply = await routeChannel
            .RequestAsync<ZLinkActorEntrySpotRouteJoinRequest, ZLinkActorEntrySpotRouteJoinReply>(
                spotNodeRid,
                ZLinkActorEntrySpotRoutePackets.JoinEntrySpotPacketName,
                request,
                registration.DefaultRequestTimeout,
                cancellationToken)
            .ConfigureAwait(false);

        var replyMessage = ZLinkActorEntrySpotRoutePackets.DecodeJoinReplyPayload(
            reply,
            registration.Codecs);
        if (!reply.Accepted)
            return new ZLinkActorJoinResult(
                false,
                null,
                replyMessage);

        var targetRef = ZLinkActorEntrySpotRoutePackets.ToActorRef(reply);
        actorState.BindNativeActorRef(targetRef);
        await NotifyManagedUserSpotLeftForEntrySpotJoinAsync(
                actor,
                previousActivation,
                cancellationToken)
            .ConfigureAwait(false);
        if (targetRef.NodeRid != sourceActorRef.NodeRid)
        {
            actorState.InvalidateContext();
            // Routed entry-spot join: the target runtime creates the actor
            // through its own claim (Takeover); this owner releases.
            await actorSessionManager.ReleaseActorLocationAfterMoveAsync(actorState, cancellationToken)
                .ConfigureAwait(false);
        }

        return new ZLinkActorJoinResult(
            true,
            targetRef.ToNative(),
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
                null,
                reply);

        actorState.BindNativeActorRef(targetRef);
        await NotifyManagedEntrySpotJoinLifecycleAsync(
                actor,
                previousActivation,
                targetRef.NodeRid,
                cancellationToken)
            .ConfigureAwait(false);
        if (targetRef.NodeRid != sourceActorRef.NodeRid)
        {
            actorState.InvalidateContext();
            // Local cross-node entry-spot move within this process keeps
            // the same owner; renew the row with the new node rid.
            await actorSessionManager.RenewActorLocationAfterEntrySpotMoveAsync(
                    actorState,
                    targetRef.NodeRid,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        return new ZLinkActorJoinResult(
            true,
            targetRef.ToNative(),
            reply);
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

        await spots.EntrySpotActors.NotifyJoinedAsync(
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
