namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkActorRemoteJoiner(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration,
    IServiceProvider services,
    ZLinkSpotRuntimeManager spots,
    ZLinkActorSessionManager actorSessionManager)
{
    public async ValueTask<ZLinkActorJoinResult> JoinAsync(
        ZLinkFrameworkRuntimeState state,
        RoutingId spotRid,
        IZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        IZLinkBackendSpotNode node,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var activation = spots.GetActivationBySpotRid(state, spotRid);
        if (activation is not null)
        {
            if (!activation.TryResolveActorJoinDescriptor(out var descriptor) || descriptor is null)
                throw new InvalidOperationException(
                    $"SPOT '{activation.SpotRid}' does not declare an actor join callback.");

            return await SubmitNativeJoinActorAsync(
                    actor,
                    actorRef,
                    node,
                    activation.NodeRid,
                    activation.SpotRid,
                    activation.ChannelName,
                    request,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        var remoteAddress = await ResolveRemoteActorJoinTargetAsync(spotRid, cancellationToken)
            .ConfigureAwait(false);
        return await SubmitRoutedJoinActorAsync(
                actor,
                actorRef,
                actorSessionManager.GetOrCreateState(actor.ActorId),
                remoteAddress.TargetNodeRid,
                remoteAddress.SpotRid,
                remoteAddress.RouterChannelId,
                request,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<ZLinkActorJoinResult> SubmitRoutedJoinActorAsync(
        IZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        ZLinkActorRuntimeState actorState,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        string routerChannelId,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(actorState.ActorType))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actor.ActorId}' does not have an actor type for remote SPOT join.");

        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            routerChannelId,
            ZLinkRemoteActorJoinPackets.RequestPacketName,
            registration.DefaultRequestTimeout);
        actorState.TryGetBoundSession(out var boundSession);
        var encodedRequest = request.Encode(registration.Codecs);
        var payload = new ZLinkRemoteActorJoinRequest(
            actor.ActorId,
            actorState.ActorType,
            boundSession.SessionNodeRid?.ToBytes().ToArray(),
            boundSession.SessionRid.Size > 0 ? boundSession.SessionRid.ToBytes().ToArray() : null,
            encodedRequest.ContentType,
            encodedRequest.Payload.ToArray());
        var parts = ZLinkEnvelopeCodec.EncodeParts(
            header,
            payload,
            typeof(ZLinkRemoteActorJoinRequest));

        var replyParts = await runtime.RequestToSpotViaRouterChannelAsync(
                routerChannelId,
                targetNodeRid,
                targetSpotRid,
                parts,
                registration.DefaultRequestTimeout,
                cancellationToken)
            .ConfigureAwait(false);
        var reply = ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<ZLinkRemoteActorJoinReply>(
            replyParts,
            "Remote actor join reply was empty.",
            $"Remote actor join failed for '{actor.ActorId}' to SPOT '{targetSpotRid}'.");
        var resultActorRef = new ZLinkBackendActorRef(
            RoutingId.From(reply.ActorNodeRid),
            reply.ActorId,
            reply.ActorGeneration);
        using var resultReply = Message.From(reply.Reply);
        var replyMessage = ZLinkMessage.FromEnvelopePayload(
            reply.ReplyContentType,
            resultReply,
            registration.Codecs);

        if (reply.Accepted)
        {
            if (resultActorRef.NodeRid != actorRef.NodeRid)
                await ApplyRemoteActorMigrationAsync(actorState, resultActorRef, cancellationToken)
                    .ConfigureAwait(false);
            else
                actorState.NativeActorRef = resultActorRef;
        }

        return new ZLinkActorJoinResult(
            reply.Accepted,
            ToActorRef(reply.Accepted ? resultActorRef : actorRef),
            replyMessage);
    }

    private async ValueTask ApplyRemoteActorMigrationAsync(
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef targetActorRef,
        CancellationToken cancellationToken)
    {
        if (ZLinkBoundSessionDispatchScope.TryDefer(
                actorState.ActorId,
                ct => ApplyRemoteActorMigrationCoreAsync(actorState, targetActorRef, ct)))
            return;

        await ApplyRemoteActorMigrationCoreAsync(actorState, targetActorRef, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask ApplyRemoteActorMigrationCoreAsync(
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef targetActorRef,
        CancellationToken cancellationToken)
    {
        actorState.NativeActorRef = targetActorRef;
        await RebindRemoteSessionActorAsync(actorState, targetActorRef, cancellationToken)
            .ConfigureAwait(false);
        actorState.InvalidateContext();
        // The target runtime claimed the actor location with Takeover as
        // part of hosting the joined instance; this owner releases its row.
        await actorSessionManager.ReleaseActorLocationAfterMoveAsync(actorState, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<ZLinkSpotRemoteAddress> ResolveRemoteActorJoinTargetAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        var resolver = services.GetService(typeof(IZLinkSpotRemoteAddressResolver))
            as IZLinkSpotRemoteAddressResolver;
        if (resolver is null) throw new InvalidOperationException($"SPOT '{spotRid}' is not active.");

        return await resolver.ResolveSpotRemoteAddressAsync(spotRid, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask<ZLinkActorJoinResult> SubmitNativeJoinActorAsync(
        IZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        IZLinkBackendSpotNode node,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        string channelName,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        var correlationId = Guid.NewGuid().ToString("N");
        var encodedRequest = request.Encode(registration.Codecs);
        var joinHeader = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            channelName,
            typeof(ZLinkMessage).Name,
            encodedRequest.ContentType,
            correlationId, null, null, null, null);
        IReadOnlyList<Message> joinParts;
        joinParts = ZLinkMessageParts.Create(
            ZLinkEnvelopeCodec.EncodeHeader(joinHeader),
            Message.From(encodedRequest.Payload.Bytes.Span));

        var tcs = new TaskCompletionSource<(ZLinkBackendActorJoinResult Result, IReadOnlyList<Message> Reply)>(
            TaskCreationOptions.RunContinuationsAsynchronously);

        using var reg = cancellationToken.Register(
            static s =>
                ((TaskCompletionSource<(ZLinkBackendActorJoinResult, IReadOnlyList<Message>)>)s!).TrySetCanceled(),
            tcs);

        if (runtime.Flow.Enabled(ZLinkMessageFlowOutcome.Sent))
            runtime.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.Sent,
                ZLinkDispatchErrorSurface.SpotActor,
                ZLinkDispatchMessageKind.ActorRequest,
                "JoinSpot",
                channelName,
                CorrelationId: correlationId,
                PeerRid: targetNodeRid.ToString(),
                SpotRid: targetSpotRid.ToString(),
                ActorId: actor.ActorId));

        var submitted = node.JoinActor(
            actorRef,
            targetNodeRid,
            targetSpotRid,
            joinParts,
            (result, reply) => tcs.TrySetResult((result, reply)),
            registration.DefaultRequestTimeout);
        ZLinkMessageParts.DisposeAll(joinParts);

        if (!submitted)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor join submit failed for '{actor.ActorId}' to SPOT '{targetSpotRid}'.");

        var (joinResult, replyParts) = await tcs.Task.ConfigureAwait(false);

        if (runtime.Flow.Enabled(ZLinkMessageFlowOutcome.ReplyReceived))
            runtime.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.ReplyReceived,
                ZLinkDispatchErrorSurface.SpotActor,
                ZLinkDispatchMessageKind.Response,
                "JoinSpot",
                channelName,
                CorrelationId: correlationId,
                PeerRid: targetNodeRid.ToString(),
                SpotRid: targetSpotRid.ToString(),
                ActorId: actor.ActorId));
        var reply = DecodeNativeJoinReply(
            joinResult.Result,
            replyParts,
            actor.ActorId,
            targetSpotRid);
        var accepted = joinResult.JoinResultCode == 0;
        var actorState = actorSessionManager.GetOrCreateState(actor.ActorId);
        var resultActor = accepted ? joinResult.Actor : actorRef;
        if (accepted)
        {
            actorState.NativeActorRef = joinResult.Actor;
            if (joinResult.Actor.NodeRid != actorRef.NodeRid) actorState.InvalidateContext();
        }

        return new ZLinkActorJoinResult(
            accepted,
            ToActorRef(resultActor),
            reply);
    }

    private async ValueTask RebindRemoteSessionActorAsync(
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef targetActorRef,
        CancellationToken cancellationToken)
    {
        if (!actorState.TryGetBoundSession(out var session)) return;

        if (!runtime.TryGetSessionActorContext(
                actorState.ActorId,
                session.BindingToken,
                out var context)
            && !runtime.TryGetSessionActorContext(actorState.ActorId, out context))
            return;

        await context.ActorCoordinator.BindActorAsync(
                context,
                ToActorRef(targetActorRef),
                cancellationToken)
            .ConfigureAwait(false);
        runtime.UnbindSessionActor(actorState.ActorId, context, session.BindingToken);
        runtime.UnbindActorSession(actorState.ActorId, session.BindingToken);
    }

    private static ActorRef ToActorRef(ZLinkBackendActorRef actorRef)
    {
        return new ActorRef(actorRef.NodeRid, actorRef.ActorId, actorRef.Generation);
    }

    private ZLinkMessage DecodeNativeJoinReply(
        RequestResult result,
        IReadOnlyList<Message> replyParts,
        string actorId,
        RoutingId spotRid)
    {
        try
        {
            if (result != RequestResult.Ok)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"Actor join was rejected for '{actorId}' to SPOT '{spotRid}'.");

            if (replyParts.Count == 0)
                throw new InvalidOperationException(
                    "Actor join reply was empty.");

            var header = ZLinkEnvelopeCodec.DecodeHeader(replyParts);
            var reply = (Message)ZLinkEnvelopeCodec.DecodeBody(replyParts, typeof(Message))!;
            using var ownedReply = Message.From(reply);
            return ZLinkMessage.FromEnvelopePayload(header.ContentType, ownedReply, registration.Codecs);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(replyParts);
        }
    }
}