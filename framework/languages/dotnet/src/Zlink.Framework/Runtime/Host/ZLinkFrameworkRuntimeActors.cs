namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    private readonly ZLinkActorStragglerForwarder _actorStragglerForwarder;

    internal ZLinkActorStragglerForwarder ActorStragglerForwarder
        => _actorStragglerForwarder;
    internal ValueTask<ZLinkActorJoinResult> JoinActorAsync(
        RoutingId spotRid,
        IZLinkActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
    {
        _drainAdmission.RequireSpotAdmission();
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
        _drainAdmission.RequireSpotAdmission();
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

    internal ValueTask<bool> DrainActorsAsync(CancellationToken cancellationToken) =>
        _actorDrainCoordinator.DrainAsync(cancellationToken);

    internal static string? ResolveActorDrainMeshName(
        ZLinkFrameworkRegistration registration,
        string actorType)
    {
        return ZLinkActorDrainCoordinator.ResolveMeshName(registration, actorType);
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
        var actorState = GetOrCreateActorState(request.ActorId);
        if (actorState.Handoff.IsQuarantined(request.HandoffId))
        {
            await _actorSessionManager.RollbackTransferredActorAsync(
                    request.ActorId,
                    CancellationToken.None)
                .ConfigureAwait(false);
            throw new ZLinkActorHandoffRejectedException(
                $"Actor '{request.ActorId}' handoff failed and its quarantined rollback was reconciled.");
        }

        if (_actorHandoffAdmissions.TryGetJoinOutcome(request, spotRid, out var terminalReply))
            return terminalReply;

        ZLinkActorTransferRegistry.TryResolve(Registration, request.ActorType, out var transfer);
        var ownsImport = false;
        var createdTransferredActor = false;
        try
        {
            if (!actorState.Handoff.IsKnown(request.HandoffId))
                _actorHandoffAdmissions.BeginCommit(request, spotRid);
            var target = ResolveActorHandoffTarget(spotRid)
                         ?? throw new InvalidOperationException(
                             $"Actor handoff target '{spotRid}' is not active.");
            var import = await actorState.ExecuteHandoffTransitionAsync(
                    () =>
                    {
                        var owned = actorState.Handoff.Import(request, out var preparation);
                        return (Owned: owned, Preparation: preparation);
                    },
                    cancellationToken)
                .ConfigureAwait(false);
            if (!import.Owned)
                return await import.Preparation.WaitAsync(cancellationToken).ConfigureAwait(false);
            ownsImport = true;

            await _actorSessionManager.PrepareForTransferredActivationAsync(
                    actorState,
                    cancellationToken)
                .ConfigureAwait(false);
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
            createdTransferredActor = creation.Created;
            var actorId = request.ActorId;
            var actorRef = actorState.NativeActorRef
                           ?? throw new ZLinkFrameworkException(
                               ZLinkFrameworkErrorKind.ActorRouteNotFound,
                               $"Actor '{actorId}' does not have a native Actor ref.");
            var boundRoute = ZLinkRemoteActorJoinPackets.DecodeBoundSessionRoute(request);
            await BindRemoteBoundSessionRouteAsync(
                    actorId,
                    actorRef,
                    boundRoute.NodeRid,
                    boundRoute.SessionRid,
                    cancellationToken)
                .ConfigureAwait(false);
            await PrepareTransferredActorTargetAsync(
                    target,
                    creation.Actor,
                    actorState,
                    cancellationToken)
                .ConfigureAwait(false);

            var reply = ZLinkRemoteActorJoinPackets.CreateJoinReply(
                true,
                actorRef);
            _actorHandoffAdmissions.RecordJoinOutcome(
                request,
                spotRid,
                reply,
                Registration.DefaultRequestTimeout);
            actorState.Handoff.AcceptPreparation(request.HandoffId, reply);
            RunDetached(
                "actor-handoff-prepared-expiry",
                ct => ReconcileExpiredPreparedHandoffAsync(
                    actorState,
                    request,
                    spotRid,
                    Registration.DefaultRequestTimeout,
                    ct));
            return reply;
        }
        catch (Exception commitFailure)
        {
            var rejected = CreateRejectedHandoffReply(request.ActorId);
            _actorHandoffAdmissions.RejectPreparedJoinOutcome(request, spotRid, rejected);
            if (ownsImport)
                actorState.Handoff.RejectPreparation(request.HandoffId, rejected);
            try
            {
                if (!ownsImport)
                {
                    // A conflicting transaction owns the actor handoff state.
                }
                else if (createdTransferredActor)
                {
                    actorState.Handoff.Quarantine(request.HandoffId);
                    await RollbackPreparedTransferredActorAsync(actorState, CancellationToken.None)
                        .ConfigureAwait(false);
                }
                else
                {
                    actorState.Handoff.AbortImport(request.HandoffId);
                }
            }
            catch (Exception rollbackFailure)
            {
                actorState.Handoff.Quarantine(request.HandoffId);
                throw new AggregateException(commitFailure, rollbackFailure);
            }
            finally
            {
                _actorHandoffAdmissions.Abort(request.HandoffId);
            }

            throw new ZLinkActorHandoffRejectedException(
                $"Actor '{request.ActorId}' handoff commit was rejected.",
                commitFailure);
        }
    }

    internal async ValueTask CompleteRoutedActorHandoffAsync(
        RoutingId spotRid,
        ZLinkRemoteActorHandoffCompletionRequest request,
        CancellationToken cancellationToken)
    {
        if (!_actorHandoffAdmissions.TryBeginCompletion(request, spotRid)) return;

        var actorState = GetOrCreateActorState(request.ActorId);

        try
        {
            var target = ResolveActorHandoffTarget(spotRid)
                         ?? throw new ZLinkFrameworkException(
                             ZLinkFrameworkErrorKind.ActorRouteNotFound,
                             $"Actor '{request.ActorId}' handoff target '{spotRid}' is not active during completion.");
            var actorRef = actorState.NativeActorRef
                           ?? throw new ZLinkFrameworkException(
                               ZLinkFrameworkErrorKind.ActorRouteNotFound,
                               $"Actor '{request.ActorId}' does not have a native Actor ref during route commit.");
            await ReplayTransferredActorHandoffAsync(
                    target,
                    actorState,
                    request.Frames,
                    cancellationToken)
                .ConfigureAwait(false);
            await PublishTransferredActorLocationAsync(actorState, target, cancellationToken)
                .ConfigureAwait(false);
            await ReplayFinalTransferredActorHandoffAsync(
                    target,
                    actorState,
                    cancellationToken)
                .ConfigureAwait(false);
            actorState.Handoff.Complete(request.HandoffId);
            await CompleteTransferredActorTargetAsync(
                    target,
                    actorState,
                    cancellationToken)
                .ConfigureAwait(false);
            _actorHandoffAdmissions.RecordCompletion(request, spotRid);
            _actorHandoffAdmissions.Complete(request.HandoffId);
            ZLinkFrameworkDebugLog.SpotDiscovery(
                $"handoff_completion actor={request.ActorId} id={request.HandoffId} frames={request.Frames.Count}");
        }
        catch
        {
            _actorHandoffAdmissions.CancelCompletion(request, spotRid);
            throw;
        }
    }

    private async ValueTask ReconcileExpiredPreparedHandoffAsync(
        ZLinkActorRuntimeState actorState,
        ZLinkRemoteActorJoinRequest request,
        RoutingId spotRid,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        await Task.Delay(timeout, cancellationToken).ConfigureAwait(false);
        var rejected = CreateRejectedHandoffReply(request.ActorId);
        while (!_actorHandoffAdmissions.TryExpirePreparedCommit(request, spotRid, rejected))
        {
            if (!_actorHandoffAdmissions.IsPreparedCommitPending(request, spotRid)) return;
            await Task.Delay(TimeSpan.FromMilliseconds(100), cancellationToken)
                .ConfigureAwait(false);
        }

        actorState.Handoff.Quarantine(request.HandoffId);
        await ZLinkReconciliationRunner.RunAsync(
                token => RollbackPreparedTransferredActorAsync(
                    actorState,
                    token,
                    startTeardownReconciliation: false),
                exception => ZLinkFrameworkDebugLog.SpotDiscovery(
                    $"expired actor handoff rollback retry for '{request.ActorId}': {exception.Message}"),
                cancellationToken,
                static exception => exception is OperationCanceledException)
            .ConfigureAwait(false);
    }

    private async ValueTask RollbackPreparedTransferredActorAsync(
        ZLinkActorRuntimeState actorState,
        CancellationToken cancellationToken,
        bool startTeardownReconciliation = true)
    {
        Exception? failure = null;
        if (actorState.Actor is { } actor)
        {
            try
            {
                if (actorState.LiveActivation is { } activation)
                    await activation.NotifyActorLeftAfterManagedJoinSpotAsync(actor, cancellationToken)
                        .ConfigureAwait(false);
                else
                    await NotifyEntrySpotActorLeftAsync(
                            actor,
                            actorState.NativeActorRef?.NodeRid,
                            cancellationToken)
                        .ConfigureAwait(false);
            }
            catch (Exception exception)
            {
                failure = exception;
            }
        }

        try
        {
            await _actorSessionManager.RollbackTransferredActorAsync(
                    actorState.ActorId,
                    cancellationToken,
                    startTeardownReconciliation)
                .ConfigureAwait(false);
        }
        catch (Exception exception)
        {
            failure = failure is null ? exception : new AggregateException(failure, exception);
        }

        if (failure is not null)
            throw new InvalidOperationException(
                $"Actor '{actorState.ActorId}' prepared handoff rollback did not finish.",
                failure);
    }

    private static ZLinkRemoteActorJoinReply CreateRejectedHandoffReply(string actorId)
        => ZLinkRemoteActorJoinPackets.CreateJoinReply(
            false,
            new ZLinkBackendActorRef(RoutingId.From("rejected"), actorId, 0));

    private async ValueTask PublishTransferredActorLocationAsync(
        ZLinkActorRuntimeState actorState,
        ActorHandoffTarget target,
        CancellationToken cancellationToken)
    {
        if (LocationLifecycle is not { } locations) return;

        var actorRef = actorState.NativeActorRef
                       ?? throw new ZLinkFrameworkException(
                           ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorState.ActorId}' does not have a native Actor ref during location commit.");
        if (target.UserSpot is { } userSpot)
            await locations.ActorOwnership.CommitTransferredActorLocationAsync(
                    actorState.ActorId,
                    actorRef.ToNative(),
                    userSpot.SpotRid,
                    cancellationToken)
                .ConfigureAwait(false);
        else
            await locations.ActorOwnership.CommitTransferredActorEntryLocationAsync(
                    actorState.ActorId,
                    actorRef.ToNative(),
                    target.NodeRid,
                    cancellationToken)
                .ConfigureAwait(false);
        LogActorHandoff(
            $"location_committed actor={actorState.ActorId} spot={target.TargetRid}");
    }

    internal async ValueTask<ZLinkRemoteActorAdmissionReply> AdmitRoutedActorJoinAsync(
        RoutingId spotRid,
        ZLinkRemoteActorAdmissionRequest request,
        CancellationToken cancellationToken = default)
    {
        if (_drainAdmission.IsDraining)
            return ZLinkRemoteActorJoinPackets.CreateAdmissionReply(
                false,
                ZLinkMessage.Empty,
                Registration.Codecs,
                request.DeadlineUnixTimeMilliseconds);
        var target = ResolveActorHandoffTarget(spotRid)
                     ?? throw new InvalidOperationException(
                         $"Actor handoff target '{spotRid}' is not active.");

        return await _actorHandoffAdmissions.AdmitAsync(
                request,
                spotRid,
                async ct =>
                {
                    var payload = ZLinkRemoteActorJoinPackets.DecodeAdmissionRequestPayload(
                        request,
                        Registration.Codecs);
                    ZLinkSpotActorJoinResult result;
                    if (target.UserSpot is { } userSpot)
                        result = await userSpot.AdmitRemoteActorJoinAsync(
                                request.ActorId,
                                payload,
                                ct)
                            .ConfigureAwait(false);
                    else if (target.EntrySpot is { } entrySpot
                             && entrySpot.TryResolveActorJoin(out var descriptor)
                             && descriptor is not null)
                        result = await entrySpot.AdmitActorJoinAsync(
                                descriptor,
                                request.ActorId,
                                payload,
                                ct)
                            .ConfigureAwait(false);
                    else
                        result = ZLinkSpotActorJoinResult.Reject();
                    return ZLinkRemoteActorJoinPackets.CreateAdmissionReply(
                        result.Accepted,
                        result.Reply,
                        Registration.Codecs,
                        request.DeadlineUnixTimeMilliseconds);
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    private ActorHandoffTarget? ResolveActorHandoffTarget(RoutingId targetRid)
    {
        var state = _state
                    ?? throw new InvalidOperationException(
                        "ZLink framework runtime is not available for actor handoff.");
        if (_spots.GetActivationBySpotRid(state, targetRid) is { } userSpot)
            return new ActorHandoffTarget(
                targetRid,
                userSpot.NodeRid,
                userSpot,
                null);
        if (state.TryGetSpotNodeByRoutingId(targetRid, out var node)
            && node.EntrySpotActivation is { } entrySpot)
            return new ActorHandoffTarget(
                targetRid,
                node.Node.RoutingId,
                null,
                entrySpot);
        return null;
    }

    private async ValueTask PrepareTransferredActorTargetAsync(
        ActorHandoffTarget target,
        IZLinkActor actor,
        ZLinkActorRuntimeState actorState,
        CancellationToken cancellationToken)
    {
        if (target.UserSpot is { } userSpot)
        {
            await userSpot.PrepareTransferredActorJoinAndReplayAsync(
                    actor,
                    actorState,
                    cancellationToken)
                .ConfigureAwait(false);
            return;
        }
    }

    private async ValueTask CompleteTransferredActorTargetAsync(
        ActorHandoffTarget target,
        ZLinkActorRuntimeState actorState,
        CancellationToken cancellationToken)
    {
        if (target.UserSpot is { } userSpot)
        {
            await userSpot.CompleteTransferredActorJoinAsync(actorState, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        var actor = actorState.Actor
                    ?? throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"Actor '{actorState.ActorId}' has no transferred instance at commit.");
        await NotifyEntrySpotActorJoinedAsync(
                actor,
                target.NodeRid,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask ReplayTransferredActorHandoffAsync(
        ActorHandoffTarget target,
        ZLinkActorRuntimeState actorState,
        IReadOnlyList<ZLinkActorHandoffFrame> sourceFrames,
        CancellationToken cancellationToken)
    {
        if (target.UserSpot is { } userSpot)
        {
            await userSpot.ReplayTransferredActorHandoffAsync(
                    actorState,
                    sourceFrames,
                    cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        var frames = actorState.Handoff.PrepareImportedReplay(sourceFrames);
        await ReplayEntrySpotActorFramesAsync(actorState, frames, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask ReplayFinalTransferredActorHandoffAsync(
        ActorHandoffTarget target,
        ZLinkActorRuntimeState actorState,
        CancellationToken cancellationToken)
    {
        if (target.UserSpot is { } userSpot)
        {
            await userSpot.ReplayFinalTransferredActorHandoffAsync(actorState, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        while (true)
        {
            var frames = actorState.Handoff.SnapshotFinalReplay();
            if (frames.Count == 0) return;
            await ReplayEntrySpotActorFramesAsync(actorState, frames, cancellationToken)
                .ConfigureAwait(false);
        }
    }

    private async ValueTask ReplayEntrySpotActorFramesAsync(
        ZLinkActorRuntimeState actorState,
        IReadOnlyList<ZLinkActorHandoffFrame> frames,
        CancellationToken cancellationToken)
    {
        if (frames.Count == 0) return;
        var actorRef = actorState.NativeActorRef
                       ?? throw new ZLinkFrameworkException(
                           ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorState.ActorId}' does not have a native Actor ref during handoff replay.");
        var pipeline = new ZLinkActorInboundPipeline(
            this,
            new ZLinkEntrySpotActorInboundEndpoint(this));
        await pipeline.DispatchReplayAsync(
                ZLinkActorHandoffFrames.Restore(actorRef, frames),
                actorState.Handoff.AcknowledgeReplayedFrame,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private readonly record struct ActorHandoffTarget(
        RoutingId TargetRid,
        RoutingId NodeRid,
        ZLinkSpotActivation? UserSpot,
        ZLinkEntrySpotActivation? EntrySpot);

    internal async ValueTask BindRemoteBoundSessionRouteAsync(
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

        var node = GetActorSpotNode()
                   ?? throw new ZLinkFrameworkException(
                       ZLinkFrameworkErrorKind.ActorSessionNotBound,
                       "Remote actor session binding requires a router-capable SpotNode.");
        node.BindRemoteActorBoundSession(actorRef, sourceNodeRid, sourceSessionRid);
        BindActorSession(
            actorId,
            sourceNodeRid,
            sourceSessionRid,
            ZLinkActorBoundSessionBindingToken.Native(sourceSessionRid));
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
        _drainAdmission.RequireActorAdmission();
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
        string bindingToken,
        CancellationToken cancellationToken = default)
    {
        var state = GetOrCreateActorState(actor.ActorId);
        if (!state.TryGetBoundSession(out var boundSession)
            || !string.Equals(
                boundSession.BindingToken,
                bindingToken,
                StringComparison.Ordinal))
            return;

        if (state.Actor is not null
            && state.NativeActorRef is { } localActor
            && localActor.NodeRid == actor.NodeRid
            && localActor.Generation == actor.Generation)
        {
            await NotifyActorDisconnectedByIdAsync(actor.ActorId, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        var node = GetActorClientSpotNode();

        await _actorBoundSessionCoordinator.NotifyRemoteDisconnectedAsync(
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
        _actorBoundSessionCoordinator.BindSessionActor(actorId, context, bindingToken, actorRef);
    }

    internal void UnbindSessionActor(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken)
    {
        _actorBoundSessionCoordinator.UnbindSessionActor(actorId, context, bindingToken);
    }

    internal bool TryGetSessionActorContext(
        string actorId,
        string bindingToken,
        out ZLinkSessionContext context)
    {
        return _actorBoundSessionCoordinator.TryGetSessionActorContext(actorId, bindingToken, out context);
    }

    internal bool TryGetSessionActorContext(
        string actorId,
        out ZLinkSessionContext context)
    {
        return _actorBoundSessionCoordinator.TryGetSessionActorContext(actorId, out context);
    }

    internal void BindActorSession(
        string actorId,
        RoutingId? sessionNodeRid,
        RoutingId sessionRid,
        string bindingToken)
    {
        _actorBoundSessionCoordinator.BindActorSession(
            actorId, sessionNodeRid, sessionRid, bindingToken);
    }

    internal void UnbindActorSession(
        string actorId,
        string bindingToken)
    {
        _actorBoundSessionCoordinator.UnbindActorSession(actorId, bindingToken);
    }

    internal void RemoveActorSessionBinding(
        string actorId,
        string bindingToken)
    {
        _actorBoundSessionCoordinator.RemoveActorSessionBinding(actorId, bindingToken);
    }

    internal void CleanupActorSessionsForSession(RoutingId sessionRid)
    {
        _actorBoundSessionCoordinator.CleanupActorSessionsForSession(sessionRid);
    }

    internal bool TryGetActorBoundSession(
        string actorId,
        out ZLinkActorBoundSession session)
    {
        return _actorBoundSessionCoordinator.TryGetActorBoundSession(actorId, out session);
    }

    private void ResetActorRuntimeGeneration()
    {
        _actorSessionManager.ResetGeneration();
        _actorHandoffAdmissions.ResetGeneration();
        _actorBoundSessionCoordinator.ResetGeneration();
    }

    internal bool SendActorBoundSession(
        string actorId,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return _actorBoundSessionCoordinator.Send(actorId, parts, flags);
    }

    internal bool SendActorBoundSessionIfCurrent(
        string actorId,
        string expectedBindingToken,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return _actorBoundSessionCoordinator.SendIfBoundTo(
            actorId,
            expectedBindingToken,
            parts,
            flags);
    }

    internal ZLinkAsyncSubmitter CreateActorBoundSessionSubmitter()
    {
        return _actorBoundSessionCoordinator.CreateSubmitter();
    }

    internal void ReplyActorNoBind(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        IReadOnlyList<Message> parts)
    {
        _actorBoundSessionCoordinator.ReplyNoBind(
            actor, sourceNodeRid, sourceSessionRid, requestId, flags, parts);
    }

    internal bool ForwardActorBoundSessionPart(
        ZLinkBackendActorRef actorRef,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        Message message,
        bool hasMore,
        SendFlags flags)
    {
        return _actorBoundSessionCoordinator.ForwardPart(
            actorRef,
            sourceNodeRid,
            sourceSessionRid,
            message,
            hasMore,
            flags);
    }

    internal ValueTask CloseActorBoundSessionAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        return _actorBoundSessionCoordinator.CloseAsync(actorId, cancellationToken);
    }
}
