namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    private static readonly ZLinkActorBoundSessionRegistry ActorBoundSessions = new();

    internal ValueTask<ZLinkActorJoinResult> JoinActorAsync(
        RoutingId spotRid,
        IZLinkActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
    {
        return _actors.JoinActorAsync(
            spotRid,
            actor,
            request,
            cancellationToken);
    }

    internal ValueTask<ZLinkActorJoinResult> JoinActorAsync(
        RoutingId spotRid,
        ActorRef actor,
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
    {
        var managedActor = ResolveOwnedActorRef(actor);
        return JoinActorAsync(spotRid, managedActor, request, cancellationToken);
    }

    internal ValueTask<ZLinkActorJoinResult> JoinActorEntrySpotAsync(
        RoutingId spotNodeRid,
        IZLinkActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
    {
        return _actors.JoinActorEntrySpotAsync(
            spotNodeRid,
            actor,
            request,
            cancellationToken);
    }

    internal ValueTask<ZLinkActorJoinResult> JoinActorEntrySpotAsync(
        RoutingId spotNodeRid,
        ActorRef actor,
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
    {
        var managedActor = ResolveOwnedActorRef(actor);
        return JoinActorEntrySpotAsync(spotNodeRid, managedActor, request, cancellationToken);
    }

    private IZLinkActor ResolveOwnedActorRef(ActorRef actor)
    {
        if (!TryGetCreatedActorState(actor.ActorId, out var state)
            || state.Actor is not { } managedActor
            || state.NativeActorRef is not { } nativeRef
            || nativeRef.NodeRid != actor.NodeRid
            || nativeRef.Generation != actor.Generation)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor ref '{actor.ActorId}' is not owned by this runtime.");

        return managedActor;
    }


    internal ValueTask DestroyActorAsync(
        RoutingId entrySpotNodeRid,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        return _actorSessionManager.DestroyActorAsync(entrySpotNodeRid, actor, cancellationToken);
    }

    internal async ValueTask<ZLinkRemoteActorJoinReply> JoinRoutedActorAsync(
        RoutingId spotRid,
        ZLinkRemoteActorJoinRequest request,
        CancellationToken cancellationToken = default)
    {
        var activation = await GetSpotActivationByRidAsync(spotRid, cancellationToken)
                             .ConfigureAwait(false)
                         ?? throw new InvalidOperationException($"SPOT '{spotRid}' is not active.");

        ZLinkActorTransferRegistry.TryResolve(Registration, request.ActorType, out var transfer);
        var actorState = GetOrCreateActorState(request.ActorId);
        actorState.ImportHandoffFrames(request.HandoffId, request.HandoffFrames);

        try
        {
            // Hosting handoff: the source node still owns the location row, so
            // the local claim may fence it out with Takeover. This path does not
            // call the Entry Spot create callback; transfer materialization is
            // not a new application-level actor creation.
            var creation = await _actorSessionManager.TransferAndBindActorAsync(
                    request.ActorId,
                    request.ActorType,
                    transfer,
                    ZLinkRemoteActorJoinPackets.DecodeTransferState(request, Registration.Codecs),
                    ZLinkActorClaimMode.TakeoverExistingOwner,
                    publishActorRef: false,
                    cancellationToken)
                .ConfigureAwait(false);
            var actorId = request.ActorId;
            var actorRef = actorState.NativeActorRef
                           ?? throw new ZLinkFrameworkException(
                               ZLinkFrameworkErrorKind.ActorRouteNotFound,
                               $"Actor '{actorId}' does not have a native Actor ref.");
            var boundRoute = ZLinkRemoteActorJoinPackets.DecodeBoundSessionRoute(request);
            await BindRemoteBoundSessionRouteAsync(
                    request.ActorId,
                    actorRef,
                    boundRoute.NodeRid,
                    boundRoute.SessionRid,
                    cancellationToken)
                .ConfigureAwait(false);
            await activation.PrepareTransferredActorJoinAndReplayAsync(
                    creation.Actor,
                    actorState,
                    cancellationToken)
                .ConfigureAwait(false);

            return ZLinkRemoteActorJoinPackets.CreateJoinReply(
                true,
                actorRef,
                ZLinkMessage.Empty,
                Registration.Codecs);
        }
        catch
        {
            actorState.CancelHandoffCapture();
            throw;
        }
    }

    internal async ValueTask CompleteRoutedActorHandoffAsync(
        ZLinkRemoteActorHandoffCompletionRequest request,
        CancellationToken cancellationToken)
    {
        var actorState = GetOrCreateActorState(request.ActorId);
        if (!actorState.TryBeginHandoffCompletion(request.HandoffId)) return;

        try
        {
            var activation = actorState.LiveActivation
                             ?? throw new ZLinkFrameworkException(
                                 ZLinkFrameworkErrorKind.ActorRouteNotFound,
                                 $"Actor '{request.ActorId}' is not hosted by an active Spot during handoff completion.");
            await activation.ReplayTransferredActorHandoffAsync(
                    actorState,
                    request.Frames,
                    cancellationToken)
                .ConfigureAwait(false);
            await PublishTransferredActorLocationAsync(actorState, activation, cancellationToken)
                .ConfigureAwait(false);
            actorState.CompleteHandoffContinuation(request.HandoffId);
            ZLinkFrameworkDebugLog.SpotDiscovery(
                $"handoff_completion actor={request.ActorId} id={request.HandoffId} frames={request.Frames.Count}");
        }
        catch
        {
            actorState.CancelHandoffContinuation(request.HandoffId);
            throw;
        }
    }

    private async ValueTask PublishTransferredActorLocationAsync(
        ZLinkActorRuntimeState actorState,
        ZLinkSpotActivation activation,
        CancellationToken cancellationToken)
    {
        if (LocationLifecycle is not { } locations) return;

        var actorRef = actorState.NativeActorRef
                       ?? throw new ZLinkFrameworkException(
                           ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorState.ActorId}' does not have a native Actor ref during location commit.");
        await locations.ActorOwnership.CommitTransferredActorLocationAsync(
                actorState.ActorId,
                actorRef.ToNative(),
                activation.SpotRid,
                cancellationToken)
            .ConfigureAwait(false);
        ZLinkFrameworkDebugLog.SpotDiscovery(
            $"location_committed actor={actorState.ActorId} spot={activation.SpotRid}");
    }

    internal async ValueTask<ZLinkRemoteActorAdmissionReply> AdmitRoutedActorJoinAsync(
        RoutingId spotRid,
        ZLinkRemoteActorAdmissionRequest request,
        CancellationToken cancellationToken = default)
    {
        var activation = await GetSpotActivationByRidAsync(spotRid, cancellationToken)
                             .ConfigureAwait(false)
                         ?? throw new InvalidOperationException($"SPOT '{spotRid}' is not active.");

        var result = await activation.AdmitRemoteActorJoinAsync(
                request.ActorId,
                ZLinkRemoteActorJoinPackets.DecodeAdmissionRequestPayload(
                    request,
                    Registration.Codecs),
                cancellationToken)
            .ConfigureAwait(false);

        return ZLinkRemoteActorJoinPackets.CreateAdmissionReply(
            result.Accepted,
            result.Reply,
            Registration.Codecs);
    }

    private async ValueTask BindRemoteBoundSessionRouteAsync(
        string actorId,
        ZLinkBackendActorRef actorRef,
        RoutingId? boundSessionNodeRid,
        RoutingId? boundSessionRid,
        CancellationToken cancellationToken)
    {
        if (boundSessionNodeRid is not { } sourceNodeRid
            || boundSessionRid is not { } sourceSessionRid)
            return;

        if (TryGetSessionActorContext(actorId, out var context))
        {
            await context.ActorCoordinator.BindActorAsync(
                    context,
                    actorRef.ToNative(),
                    cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        BindActorSession(
            actorId,
            sourceNodeRid,
            sourceSessionRid,
            ZLinkActorBoundSessionBindingToken.Native(sourceSessionRid));
        var node = GetActorSpotNode()
                   ?? throw new ZLinkFrameworkException(
                       ZLinkFrameworkErrorKind.ActorSessionNotBound,
                       "Remote actor session binding requires a router-capable SpotNode.");
        node.BindRemoteActorBoundSession(actorRef, sourceNodeRid, sourceSessionRid);
    }

    internal ValueTask JoinActorToSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        return _actorSessionManager.JoinActorToSpotAsync(activation, actor, cancellationToken);
    }

    internal ValueTask AttachActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
    {
        return _actorSessionManager.AttachActorAsync(actor, stream, cancellationToken);
    }

    internal ValueTask DisconnectActorAsync(
        IZLinkActor actor,
        IZLinkStream stream,
        CancellationToken cancellationToken = default)
    {
        return _actorSessionManager.DisconnectActorAsync(actor, stream, cancellationToken);
    }

    internal ValueTask SubmitActorAsync(
        IZLinkActor actor,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        return _actorSessionManager.SubmitActorAsync(actor, header, payload, cancellationToken);
    }

    internal ValueTask<CreateActorResult> CreateLocalActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        return CreateLocalActorAsync(
                actorId,
                actorType,
                ZLinkMessage.Empty,
                cancellationToken);
    }

    internal ValueTask<CreateActorResult> CreateLocalActorAsync(
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken = default)
    {
        return CreateLocalActorAsync(
            actorId,
            actorType,
            createRequest,
            ZLinkActorClaimMode.NewOwner,
            cancellationToken);
    }

    internal ValueTask<CreateActorResult> CreateLocalActorForHandoffAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        return CreateLocalActorAsync(
            actorId,
            actorType,
            ZLinkMessage.Empty,
            ZLinkActorClaimMode.TakeoverExistingOwner,
            cancellationToken);
    }

    private async ValueTask<CreateActorResult> CreateLocalActorAsync(
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        ZLinkActorClaimMode claimMode,
        CancellationToken cancellationToken)
    {
        var result = await _actorSessionManager.CreateAndBindActorAsync(
                actorId,
                actorType,
                createRequest,
                claimMode,
                cancellationToken)
            .ConfigureAwait(false);
        if (result.Created)
        {
            var state = GetOrCreateActorState(result.Actor.ActorId);
            var nativeRef = state.NativeActorRef
                            ?? throw new ZLinkFrameworkException(
                                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                                $"Actor '{result.Actor.ActorId}' does not have a native Actor ref after creation.");
            await NotifyEntrySpotActorCreatedAsync(
                    result.Actor,
                    result.CreateRequest,
                    nativeRef.NodeRid,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        return result;
    }

    internal ValueTask<CreateActorResult> CreateActorAsync(
        string actorId,
        string actorType,
        CancellationToken cancellationToken = default)
    {
        return CreateActorAsync(actorId, actorType, ZLinkMessage.Empty, cancellationToken);
    }

    internal ValueTask<CreateActorResult> CreateActorAsync(
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken = default)
    {
        return _actorSessionManager.CreateActorAsync(actorId, actorType, createRequest, cancellationToken);
    }

    internal ValueTask<IZLinkActor?> FindActorAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        return _actorSessionManager.FindActorAsync(actorId, cancellationToken);
    }

    internal bool TryGetCreatedActor(
        string actorId,
        string actorType,
        out IZLinkActor actor)
    {
        return _actorSessionManager.TryGetCreatedActor(actorId, actorType, out actor);
    }

    internal bool TryGetCreatedActorState(
        string actorId,
        out ZLinkActorRuntimeState state)
    {
        return _actorSessionManager.TryGetCreatedActorState(actorId, out state);
    }

    internal bool TryGetCreatedActorState(
        string actorId,
        string actorType,
        out ZLinkActorRuntimeState state)
    {
        return _actorSessionManager.TryGetCreatedActorState(actorId, actorType, out state);
    }

    internal ValueTask<ZLinkActorReply> SubmitActorForReplyAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        return _actorSessionManager.SubmitActorForReplyAsync(actorId, header, payload, cancellationToken);
    }

    internal ValueTask SubmitActorByIdAsync(
        string actorId,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken = default)
    {
        return _actorSessionManager.SubmitActorByIdAsync(actorId, header, payload, cancellationToken);
    }

    internal ValueTask NotifyActorDisconnectedByIdAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        return _actorSessionManager.NotifyDisconnectedByIdAsync(actorId, cancellationToken);
    }

    internal async ValueTask NotifyActorDisconnectedAsync(
        ActorRef actor,
        CancellationToken cancellationToken = default)
    {
        var state = GetOrCreateActorState(actor.ActorId);
        if (state.Actor is not null
            && state.NativeActorRef is { } localActor
            && localActor.NodeRid == actor.NodeRid
            && localActor.Generation == actor.Generation)
        {
            await NotifyActorDisconnectedByIdAsync(actor.ActorId, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        if (GetActorSpotNode() is not { } node)
        {
            await NotifyActorDisconnectedByIdAsync(actor.ActorId, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        await NotifyRemoteActorDisconnectedAsync(
                state,
                actor.ToBackend(),
                node,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal ZLinkActorRuntimeState GetOrCreateActorState(string actorId)
    {
        return _actorSessionManager.GetOrCreateState(actorId);
    }

    internal ValueTask DeactivateActorOnOwnershipLossAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        return _actorSessionManager.DeactivateActorOnOwnershipLossAsync(actorId, cancellationToken);
    }

    internal ZLinkLocationLifecycle? LocationLifecycle => _locationLifecycle;

    internal void BindSessionActor(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken,
        ZLinkSessionActor actorRef)
    {
        _sessionBindings.Bind(actorId, context, bindingToken, actorRef);
    }

    internal void UnbindSessionActor(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken)
    {
        _sessionBindings.Unbind(actorId, context, bindingToken);
    }

    internal bool TryGetSessionActorContext(
        string actorId,
        string bindingToken,
        out ZLinkSessionContext context)
    {
        return _sessionBindings.TryGet(actorId, bindingToken, out context);
    }

    internal bool TryGetSessionActorContext(
        string actorId,
        out ZLinkSessionContext context)
    {
        return _sessionBindings.TryGetByActorId(actorId, out context);
    }

    internal void BindActorSession(
        string actorId,
        RoutingId? sessionNodeRid,
        RoutingId sessionRid,
        string bindingToken)
    {
        GetOrCreateActorState(actorId).BindSession(sessionNodeRid, sessionRid, bindingToken);
        ActorBoundSessions.Register(this, actorId, sessionRid, bindingToken);
        LocationLifecycle?.ActorSessionRoutes.OnActorSessionBound(
            sessionRid,
            actorId,
            GetActorSpotNode()?.RoutingId ?? default);
    }

    internal void UnbindActorSession(
        string actorId,
        string bindingToken)
    {
        NotifyActorSessionRouteUnbound(actorId, bindingToken);
        GetOrCreateActorState(actorId).UnbindSession(bindingToken);
        ActorBoundSessions.Unregister(this, actorId, bindingToken);
    }

    internal void RemoveActorSessionBinding(
        string actorId,
        string bindingToken)
    {
        if (TryGetSessionActorContext(actorId, bindingToken, out var context))
            UnbindSessionActor(actorId, context, bindingToken);

        NotifyActorSessionRouteUnbound(actorId, bindingToken);
        GetOrCreateActorState(actorId).UnbindSession(bindingToken);
        ActorBoundSessions.Unregister(this, actorId, bindingToken);
    }

    private void NotifyActorSessionRouteUnbound(string actorId, string bindingToken)
    {
        if (LocationLifecycle is not { } lifecycle) return;

        if (GetOrCreateActorState(actorId).TryGetBoundSession(out var session)
            && string.Equals(session.BindingToken, bindingToken, StringComparison.Ordinal))
            lifecycle.ActorSessionRoutes.OnActorSessionUnbound(session.SessionRid);
    }

    internal void CleanupActorSessionsForSession(RoutingId sessionRid)
    {
        ActorBoundSessions.Cleanup(sessionRid);
    }

    internal bool TryGetActorBoundSession(
        string actorId,
        out ZLinkActorBoundSession session)
    {
        return GetOrCreateActorState(actorId).TryGetBoundSession(out session);
    }

    internal bool SendActorBoundSession(
        string actorId,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        var state = GetOrCreateActorState(actorId);
        var node = GetActorSpotNode()
                   ?? throw new ZLinkFrameworkException(
                       ZLinkFrameworkErrorKind.ActorSessionNotBound,
                       "Actor bound session send requires a router-capable SpotNode.",
                       false);
        var actorRef = state.NativeActorRef
                       ?? throw new ZLinkFrameworkException(
                           ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorId}' does not have a native Actor ref.",
                           false);

        return node.SendActorBoundSession(actorRef, parts, flags);
    }

    internal void ReplyActorNoBind(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        IReadOnlyList<Message> parts)
    {
        var node = GetActorSpotNode()
                   ?? throw new ZLinkFrameworkException(
                       ZLinkFrameworkErrorKind.ActorSessionNotBound,
                       "Actor no-bind reply requires a router-capable SpotNode.",
                       false);
        node.ReplyActorNoBind(actor, sourceNodeRid, sourceSessionRid, requestId, flags, parts);
    }

    internal bool ForwardActorBoundSessionPart(
        ZLinkBackendActorRef actorRef,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        Message message,
        bool hasMore,
        SendFlags flags)
    {
        var node = GetActorSpotNode()
                   ?? throw new ZLinkFrameworkException(
                       ZLinkFrameworkErrorKind.ActorRouteNotFound,
                       "Actor session forward requires a router-capable SpotNode.",
                       false);

        return node.ForwardActorBoundSessionPart(
            actorRef,
            sourceNodeRid,
            sourceSessionRid,
            message,
            hasMore,
            flags);
    }

    private ValueTask NotifyRemoteActorDisconnectedAsync(
        ZLinkActorRuntimeState state,
        ZLinkBackendActorRef actorRef,
        IZLinkBackendSpotNode node,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!state.TryGetBoundSession(out var session)
            || session.SessionNodeRid is not { } sourceNodeRid)
        {
            node.CloseActorBoundSession(
                actorRef,
                Registration.DefaultRequestTimeout,
                cancellationToken);
            return ValueTask.CompletedTask;
        }

        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Raw,
            ZlinkStreamHeaderFlags.None,
            null,
            ZLinkRemoteActorJoinPackets.SessionDisconnectedPacketName,
            ZlinkStreamMetadata.Empty);
        var headerBytes = ZLinkStreamProtocolDefaults.EncodeHeader(header).ToArray();
        using var headerPart = Message.From(headerBytes);
        using var bodyPart = Message.From(Array.Empty<byte>());

        if (!node.ForwardActorBoundSessionPart(
                actorRef,
                sourceNodeRid,
                session.SessionRid,
                headerPart,
                true,
                SendFlags.DontWait))
            throw new InvalidOperationException("Actor session disconnect header forward failed.");

        if (!node.ForwardActorBoundSessionPart(
                actorRef,
                sourceNodeRid,
                session.SessionRid,
                bodyPart,
                false,
                SendFlags.DontWait))
            throw new InvalidOperationException("Actor session disconnect body forward failed.");

        return ValueTask.CompletedTask;
    }

    internal ValueTask CloseActorBoundSessionAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        var state = GetOrCreateActorState(actorId);
        var node = GetActorSpotNode()
                   ?? throw new ZLinkFrameworkException(
                       ZLinkFrameworkErrorKind.ActorSessionNotBound,
                       "Actor bound session close requires a router-capable SpotNode.",
                       false);
        var actorRef = state.NativeActorRef
                       ?? throw new ZLinkFrameworkException(
                           ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorId}' does not have a native Actor ref.",
                           false);

        node.CloseActorBoundSession(
            actorRef,
            Registration.DefaultRequestTimeout,
            cancellationToken);
        return ValueTask.CompletedTask;
    }
}
