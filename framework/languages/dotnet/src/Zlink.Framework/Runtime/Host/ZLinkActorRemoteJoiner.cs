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

        ZLinkActorTransferRegistry.TryResolve(registration, actorState.ActorType, out var transfer);

        var sourceSpotRid = ResolveSourceSpotRid(actorState);

        var admissionHeader = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            routerChannelId,
            ZLinkRemoteActorJoinPackets.AdmissionPacketName,
            registration.DefaultRequestTimeout);
        var admissionParts = ZLinkRemoteActorJoinPackets.EncodeAdmissionRequest(
            admissionHeader,
            actor.ActorId,
            actorState.ActorType,
            sourceSpotRid,
            actorRef.NodeRid,
            request,
            registration.Codecs);
        var admissionReplyParts = await runtime.RequestToSpotViaRouterChannelAsync(
                routerChannelId,
                targetNodeRid,
                targetSpotRid,
                admissionParts,
                registration.DefaultRequestTimeout,
                cancellationToken)
            .ConfigureAwait(false);
        var admissionReply = ZLinkRemoteActorJoinPackets.DecodeAdmissionReplyAndDispose(
            admissionReplyParts,
            actor.ActorId,
            targetSpotRid);
        var admissionReplyMessage = ZLinkRemoteActorJoinPackets.DecodeAdmissionReplyPayload(
            admissionReply,
            registration.Codecs);
        if (!admissionReply.Accepted)
            return new ZLinkActorJoinResult(false, null, admissionReplyMessage);

        var transferState = transfer is null
            ? ZLinkMessage.Empty
            : await ZLinkActorTransferRegistry.TransferOutAsync(
                    services,
                    transfer,
                    actor,
                    cancellationToken)
                .ConfigureAwait(false);

        await NotifySourceActorLeftAsync(actor, actorState, cancellationToken)
            .ConfigureAwait(false);

        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            routerChannelId,
            ZLinkRemoteActorJoinPackets.CommitPacketName,
            registration.DefaultRequestTimeout);
        actorState.TryGetBoundSession(out var boundSession);
        var parts = ZLinkRemoteActorJoinPackets.EncodeJoinRequest(
            header,
            actor.ActorId,
            actorState.ActorType,
            boundSession.SessionNodeRid,
            boundSession.SessionRid,
            transferState,
            request,
            registration.Codecs);

        var replyParts = await runtime.RequestToSpotViaRouterChannelAsync(
                routerChannelId,
                targetNodeRid,
                targetSpotRid,
                parts,
                registration.DefaultRequestTimeout,
                cancellationToken)
            .ConfigureAwait(false);
        var reply = ZLinkRemoteActorJoinPackets.DecodeJoinReplyAndDispose(
            replyParts,
            actor.ActorId,
            targetSpotRid);
        var resultActorRef = ZLinkRemoteActorJoinPackets.ToActorRef(reply);
        var replyMessage = ZLinkRemoteActorJoinPackets.DecodeJoinReplyPayload(
            reply,
            registration.Codecs);

        if (reply.Accepted)
            await ApplyRemoteActorMigrationAsync(actor, actorState, resultActorRef, cancellationToken)
                .ConfigureAwait(false);

        return new ZLinkActorJoinResult(
            reply.Accepted,
            reply.Accepted ? resultActorRef.ToNative() : null,
            admissionReplyMessage);
    }

    private RoutingId ResolveSourceSpotRid(ZLinkActorRuntimeState actorState)
    {
        if (actorState.LiveActivation is { } activation) return activation.SpotRid;

        foreach (var spotNode in registration.SpotNodes.Values)
            if (spotNode.EntrySpotOptions.RoutingId.Size > 0)
                return spotNode.EntrySpotOptions.RoutingId;

        return actorState.NativeActorRef?.NodeRid ?? default;
    }

    private async ValueTask ApplyRemoteActorMigrationAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef targetActorRef,
        CancellationToken cancellationToken)
    {
        if (ZLinkBoundSessionDispatchScope.TryDefer(
                actorState.ActorId,
                ct => ApplyRemoteActorMigrationCoreAsync(actor, actorState, targetActorRef, ct)))
            return;

        await ApplyRemoteActorMigrationCoreAsync(actor, actorState, targetActorRef, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask ApplyRemoteActorMigrationCoreAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef targetActorRef,
        CancellationToken cancellationToken)
    {
        _ = actor;
        actorState.BindNativeActorRef(targetActorRef);
        await RebindRemoteSessionActorAsync(actorState, targetActorRef, cancellationToken)
            .ConfigureAwait(false);
        actorState.InvalidateContext();
        // The target runtime claimed the actor location with Takeover as
        // part of hosting the joined instance; releasing this owner's stale
        // row is cleanup and must not hold back the accepted join reply.
        _ = ReleaseActorLocationAfterMoveAsync(actorState);
    }

    private async ValueTask NotifySourceActorLeftAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState actorState,
        CancellationToken cancellationToken)
    {
        if (actorState.LiveActivation is { } previousActivation)
            await previousActivation.NotifyActorLeftAfterManagedJoinSpotAsync(actor, cancellationToken)
                .ConfigureAwait(false);
        else
            await runtime.NotifyEntrySpotActorLeftAsync(actor, cancellationToken: cancellationToken)
                .ConfigureAwait(false);
    }

    private async Task ReleaseActorLocationAfterMoveAsync(
        ZLinkActorRuntimeState actorState)
    {
        try
        {
            await actorSessionManager.ReleaseActorLocationAfterMoveAsync(actorState, CancellationToken.None)
                .ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            ZLinkFrameworkDebugLog.SpotDiscovery(
                $"remote actor move cleanup failed for '{actorState.ActorId}': {exception.Message}");
        }
    }

    private async ValueTask<ZLinkSpotRouteRef> ResolveRemoteActorJoinTargetAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        var resolver = services.GetService(typeof(IZLinkSpotRouteRefResolver))
            as IZLinkSpotRouteRefResolver;
        if (resolver is null) throw new InvalidOperationException($"SPOT '{spotRid}' is not active.");

        return await resolver.ResolveSpotRouteRefAsync(spotRid, cancellationToken)
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
        if (accepted)
        {
            actorState.BindNativeActorRef(joinResult.Actor);
            if (joinResult.Actor.NodeRid != actorRef.NodeRid) actorState.InvalidateContext();
        }

        return new ZLinkActorJoinResult(
            accepted,
            accepted ? joinResult.Actor.ToNative() : null,
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
                targetActorRef.ToNative(),
                cancellationToken)
            .ConfigureAwait(false);
        runtime.UnbindSessionActor(actorState.ActorId, context, session.BindingToken);
        runtime.UnbindActorSession(actorState.ActorId, session.BindingToken);
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
