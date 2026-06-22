using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Spots;
using Zlink.Framework.Runtime.Streams;

namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkFrameworkActorFacade(
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration,
    IServiceProvider services,
    ZLinkSpotRuntimeManager spots,
    ZLinkActorSessionManager actorSessionManager,
    Func<ZLinkFrameworkRuntimeState> getState,
    Func<IZLinkBackendSpotNode?> getActorSpotNode)
{
    private readonly ZLinkActorEntrySpotJoinCoordinator _entrySpotJoin = new(
        registration,
        spots,
        actorSessionManager,
        getState,
        getActorSpotNode,
        runtime.Flow);

    public async ValueTask<ZLinkActorJoinResult> JoinActorAsync(
        RoutingId spotRid,
        IZLinkActor actor,
        Message request,
        CancellationToken cancellationToken = default)
    {
        var state = getState();
        var actorState = actorSessionManager.GetOrCreateState(actor.ActorId);
        var node = getActorSpotNode();
        var localActivation = spots.GetActivationBySpotRid(state, spotRid);

        if (localActivation is null
            && node is not null
            && actorState.NativeActorRef is { } actorRef)
        {
            return await NativeJoinActorAsync(
                state,
                spotRid,
                actor,
                actorRef,
                node,
                request,
                cancellationToken).ConfigureAwait(false);
        }

        ZLinkSpotActorJoinResult joinResult;
        if (localActivation is not null)
        {
            joinResult = await localActivation.JoinActorAsync(actor, request, cancellationToken)
                .ConfigureAwait(false);
        }
        else
        {
            joinResult = await spots.JoinActorAsync(
                state,
                spotRid,
                actor,
                request,
                cancellationToken).ConfigureAwait(false);
        }
        return new ZLinkActorJoinResult(
            Accepted: joinResult.Accepted,
            ToActorRef(actorState),
            joinResult.Reply ?? Message.From(ReadOnlySpan<byte>.Empty));
    }

    public async ValueTask<ZLinkActorJoinResult> JoinActorEntrySpotAsync(
        RoutingId spotNodeRid,
        IZLinkActor actor,
        Message request,
        CancellationToken cancellationToken = default)
    {
        return await _entrySpotJoin.JoinAsync(spotNodeRid, actor, request, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask DestroyActorAsync(
        RoutingId entrySpotNodeRid,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        await actorSessionManager.DestroyActorAsync(entrySpotNodeRid, actor, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask JoinActorToSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        await actorSessionManager.JoinActorToSpotAsync(activation, actor, cancellationToken);
    }

    public async ValueTask AttachActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
    {
        await actorSessionManager.AttachActorAsync(actor, stream, cancellationToken);
    }

    public async ValueTask DisconnectActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
    {
        await actorSessionManager.DisconnectActorAsync(actor, stream, cancellationToken);
    }

    public async ValueTask SubmitActorAsync(
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        await actorSessionManager.SubmitActorAsync(actor, header, payload, cancellationToken);
    }

    public async ValueTask<CreateActorResult> CreateLocalActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        return await actorSessionManager.CreateAndBindActorAsync(actorId, actorType, cancellationToken);
    }

    public async ValueTask<CreateActorResult> CreateActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        return await actorSessionManager.CreateActorAsync(actorId, actorType, cancellationToken);
    }

    internal bool TryGetCreatedActorState(
        string actorId,
        out ZLinkActorRuntimeState state)
    {
        return actorSessionManager.TryGetCreatedActorState(actorId, out state);
    }

    internal bool TryGetCreatedActorState(
        string actorId,
        string actorType,
        out ZLinkActorRuntimeState state)
    {
        return actorSessionManager.TryGetCreatedActorState(actorId, actorType, out state);
    }

    public async ValueTask<IZLinkActor?> FindActorAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        return await actorSessionManager.FindActorAsync(actorId, cancellationToken);
    }

    public bool TryGetCreatedActor(
        string actorId,
        string actorType,
        out IZLinkActor actor)
    {
        return actorSessionManager.TryGetCreatedActor(actorId, actorType, out actor);
    }

    public async ValueTask<byte[]> SubmitActorForReplyAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        var reply = await actorSessionManager.SubmitActorForReplyAsync(actorId, header, payload, cancellationToken)
            .ConfigureAwait(false);
        return reply.Payload;
    }

    internal async ValueTask<ZLinkActorReply> SubmitActorForReplyCoreAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        return await actorSessionManager.SubmitActorForReplyAsync(actorId, header, payload, cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask SubmitActorByIdAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        await actorSessionManager.SubmitActorByIdAsync(actorId, header, payload, cancellationToken);
    }

    public async ValueTask NotifyDisconnectedByIdAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        await actorSessionManager.NotifyDisconnectedByIdAsync(actorId, cancellationToken);
    }

    public ZLinkActorRuntimeState GetOrCreateActorState(string actorId)
    {
        return actorSessionManager.GetOrCreateState(actorId);
    }

    private async ValueTask<ZLinkActorJoinResult> NativeJoinActorAsync(
        ZLinkFrameworkRuntimeState state,
        RoutingId spotRid,
        IZLinkActor actor,
        ZLinkBackendActorRef actorRef,
        IZLinkBackendSpotNode node,
        Message request,
        CancellationToken cancellationToken)
    {
        var activation = spots.GetActivationBySpotRid(state, spotRid);
        if (activation is not null)
        {
            if (!activation.TryResolveActorJoinDescriptor(out var descriptor) || descriptor is null)
            {
                throw new InvalidOperationException(
                    $"SPOT '{activation.SpotRid}' does not declare an actor join callback.");
            }

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
        Message request,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(actorState.ActorType))
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actor.ActorId}' does not have an actor type for remote SPOT join.");
        }

        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            routerChannelId,
            ZLinkRemoteActorJoinPackets.RequestPacketName,
            registration.DefaultRequestTimeout);
        actorState.TryGetBoundSession(out var boundSession);
        var payload = new ZLinkRemoteActorJoinRequest(
            actor.ActorId,
            actorState.ActorType,
            boundSession.SessionNodeRid?.ToBytes().ToArray(),
            boundSession.SessionRid.Size > 0 ? boundSession.SessionRid.ToBytes().ToArray() : null,
            request.ToArray());
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
        var resultReply = Message.From(reply.Reply);

        if (reply.Accepted)
        {
            if (resultActorRef.NodeRid != actorRef.NodeRid)
            {
                await ApplyRemoteActorMigrationAsync(actorState, resultActorRef, cancellationToken)
                    .ConfigureAwait(false);
            }
            else
            {
                actorState.NativeActorRef = resultActorRef;
            }
        }

        return new ZLinkActorJoinResult(
            reply.Accepted,
            ToActorRef(reply.Accepted ? resultActorRef : actorRef),
            resultReply);
    }

    private async ValueTask ApplyRemoteActorMigrationAsync(
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef targetActorRef,
        CancellationToken cancellationToken)
    {
        if (ZLinkBoundSessionDispatchScope.TryDefer(
                actorState.ActorId,
                ct => ApplyRemoteActorMigrationCoreAsync(actorState, targetActorRef, ct)))
        {
            return;
        }

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
    }

    private async ValueTask<ZLinkSpotRemoteAddress> ResolveRemoteActorJoinTargetAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        var resolver = services.GetService(typeof(IZLinkSpotRemoteAddressResolver))
            as IZLinkSpotRemoteAddressResolver;
        if (resolver is null)
        {
            throw new InvalidOperationException($"SPOT '{spotRid}' is not active.");
        }

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
        Message request,
        CancellationToken cancellationToken)
    {
        var correlationId = Guid.NewGuid().ToString("N");
        var joinHeader = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Request,
            channelName,
            typeof(Message).Name,
            ZLinkEnvelopeCodec.DefaultContentType,
            correlationId, null, null, null, null);
        var joinParts = ZLinkEnvelopeCodec.EncodeParts(joinHeader, request, typeof(Message));

        var tcs = new TaskCompletionSource<(ZLinkBackendActorJoinResult Result, IReadOnlyList<Message> Reply)>(
            TaskCreationOptions.RunContinuationsAsynchronously);

        using var reg = cancellationToken.Register(
            static s => ((TaskCompletionSource<(ZLinkBackendActorJoinResult, IReadOnlyList<Message>)>)s!).TrySetCanceled(),
            tcs);

        if (runtime.Flow.Enabled(ZLinkMessageFlowPhase.Sent))
        {
            runtime.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowPhase.Sent,
                ZLinkDispatchErrorSurface.SpotActor,
                ZLinkDispatchMessageKind.ActorRequest,
                PacketName: "JoinSpot",
                ChannelName: channelName,
                CorrelationId: correlationId,
                SourceRid: targetNodeRid.ToString(),
                SpotRid: targetSpotRid.ToString(),
                ActorId: actor.ActorId));
        }

        var submitted = node.JoinActor(
            actorRef,
            targetNodeRid,
            targetSpotRid,
            joinParts,
            (result, reply) => tcs.TrySetResult((result, reply)),
            registration.DefaultRequestTimeout);
        ZLinkMessageParts.DisposeAll(joinParts);

        if (!submitted)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor join submit failed for '{actor.ActorId}' to SPOT '{targetSpotRid}'.");
        }

        var (joinResult, replyParts) = await tcs.Task.ConfigureAwait(false);

        if (runtime.Flow.Enabled(ZLinkMessageFlowPhase.ReplyReceived))
        {
            runtime.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowPhase.ReplyReceived,
                ZLinkDispatchErrorSurface.SpotActor,
                ZLinkDispatchMessageKind.Response,
                PacketName: "JoinSpot",
                ChannelName: channelName,
                CorrelationId: correlationId,
                SourceRid: targetNodeRid.ToString(),
                SpotRid: targetSpotRid.ToString(),
                ActorId: actor.ActorId));
        }
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
            if (joinResult.Actor.NodeRid != actorRef.NodeRid)
            {
                actorState.InvalidateContext();
            }
        }

        return new ZLinkActorJoinResult(
            Accepted: accepted,
            ToActorRef(resultActor),
            reply);
    }

    private async ValueTask RebindRemoteSessionActorAsync(
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef targetActorRef,
        CancellationToken cancellationToken)
    {
        if (!actorState.TryGetBoundSession(out var session))
        {
            return;
        }

        if (!runtime.TryGetSessionActorContext(
                actorState.ActorId,
                session.BindingToken,
                out var context)
            && !runtime.TryGetSessionActorContext(actorState.ActorId, out context))
        {
            return;
        }

        await context.ActorCoordinator.BindActorAsync(
                context,
                ToActorRef(targetActorRef),
                cancellationToken)
            .ConfigureAwait(false);
        runtime.UnbindSessionActor(actorState.ActorId, context, session.BindingToken);
        runtime.UnbindActorSession(actorState.ActorId, session.BindingToken);
    }

    private static ActorRef ToActorRef(ZLinkActorRuntimeState actorState)
    {
        var actorRef = actorState.NativeActorRef
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorState.ActorId}' does not have a native Actor ref.");
        return ToActorRef(actorRef);
    }

    private static ActorRef ToActorRef(ZLinkBackendActorRef actorRef)
        => new(actorRef.NodeRid, actorRef.ActorId, actorRef.Generation);

    private static Message DecodeNativeJoinReply(
        RequestResult result,
        IReadOnlyList<Message> replyParts,
        string actorId,
        RoutingId spotRid)
    {
        try
        {
            if (result != RequestResult.Ok)
            {
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"Actor join was rejected for '{actorId}' to SPOT '{spotRid}'.");
            }

            if (replyParts.Count == 0)
            {
                throw new InvalidOperationException(
                    "Actor join reply was empty.");
            }

            var reply = (Message)ZLinkEnvelopeCodec.DecodeBody(replyParts, typeof(Message))!;
            return Message.From(reply);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(replyParts);
        }
    }
}
