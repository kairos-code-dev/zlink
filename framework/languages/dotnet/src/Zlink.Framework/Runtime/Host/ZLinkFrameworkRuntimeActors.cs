namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    private readonly ZLinkActorStragglerForwarder _actorStragglerForwarder;

    internal ZLinkActorStragglerForwarder ActorStragglerForwarder
        => _actorStragglerForwarder;
    internal ValueTask<ZLinkActorJoinResult> JoinActorAsync(
        string spotId,
        IZLinkActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
    {
        return JoinActorAsync(spotId, actor, request, operationId: null, cancellationToken);
    }

    internal ValueTask<ZLinkActorJoinResult> JoinActorAsync(
        string spotId,
        IZLinkActor actor,
        ZLinkMessage request,
        ZLinkActorJoinOperationId? operationId,
        CancellationToken cancellationToken = default)
    {
        _drainAdmission.RequireSpotAdmission();
        return _actors.JoinActorAsync(
            spotId,
            actor,
            request,
            operationId,
            cancellationToken);
    }

    internal ValueTask<ZLinkActorJoinResult> JoinActorAsync(
        string spotId,
        ActorRef actor,
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
    {
        var managedActor = ResolveOwnedActorRef(actor);
        return JoinActorAsync(spotId, managedActor, request, cancellationToken);
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

    internal async ValueTask<ZLinkFrameworkTerminationReason?> PreflightRetireAsync(
        CancellationToken cancellationToken)
    {
        await WaitForAcceptedActorHandoffsAsync(cancellationToken).ConfigureAwait(false);
        return await _actorDrainCoordinator.PreflightAsync(cancellationToken)
            .ConfigureAwait(false);
    }

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
        string spotId,
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

        if (_actorHandoffAdmissions.TryGetJoinOutcome(request, spotId, out var terminalReply))
            return terminalReply;

        ZLinkActorTransferRegistry.TryResolve(Registration, request.ActorType, out var transfer);
        var ownsImport = false;
        var createdTransferredActor = false;
        try
        {
            if (!actorState.Handoff.IsKnown(request.HandoffId))
                _actorHandoffAdmissions.BeginCommit(request, spotId);
            var target = ResolveActorHandoffTarget(spotId)
                         ?? throw new InvalidOperationException(
                             $"Actor handoff target '{spotId}' is not active.");
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
            // The joined callback belongs to the commit phase (spec 23 §10):
            // its failure must reject the commit so the source keeps its
            // backlog and rolls back, and the source's capture window must
            // stay open while the callback runs — the commit reply is what
            // moves the source from capturing to forwarding.
            await CompleteTransferredActorTargetAsync(
                    target,
                    actorState,
                    cancellationToken)
                .ConfigureAwait(false);

            var reply = ZLinkRemoteActorJoinPackets.CreateJoinReply(
                true,
                actorRef);
            _actorHandoffAdmissions.RecordJoinOutcome(
                request,
                spotId,
                reply,
                Registration.DefaultRequestTimeout);
            actorState.Handoff.AcceptPreparation(request.HandoffId, reply);
            RunDetached(
                "actor-handoff-prepared-expiry",
                ct => ReconcileExpiredPreparedHandoffAsync(
                    actorState,
                    request,
                    spotId,
                    Registration.DefaultRequestTimeout,
                    ct));
            return reply;
        }
        catch (Exception commitFailure)
        {
            var rejected = CreateRejectedHandoffReply(request.ActorId);
            _actorHandoffAdmissions.RejectPreparedJoinOutcome(request, spotId, rejected);
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
        string spotId,
        ZLinkRemoteActorHandoffCompletionRequest request,
        CancellationToken cancellationToken)
    {
        var completionJournal = CreateDeferredJoinCompletionJournal();
        ZLinkDeferredJoinCompletionRoot? completionRoot = null;
        if (completionJournal is not null
            && (request.OperationIdHigh != 0 || request.OperationIdLow != 0))
            completionRoot = await completionJournal.RecoverAsync(
                    request.ActorId,
                    cancellationToken)
                .ConfigureAwait(false);

        var ownsRecordedCompletion = false;
        try
        {
            ownsRecordedCompletion = _actorHandoffAdmissions.TryBeginCompletion(request, spotId);
        }
        catch (ZLinkFrameworkException) when (completionRoot is not null)
        {
            // A restarted target reconstructs post-commit work from the
            // published relocation root instead of its process-local
            // admission table.
        }
        if (!ownsRecordedCompletion && completionRoot is null) return;
        if (!ownsRecordedCompletion
            && completionRoot is
            {
                Completion.Cursor: ZLinkDeferredJoinCompletionCursor.Delivered
            })
            return;

        var actorState = GetOrCreateActorState(request.ActorId);

        try
        {
            var target = ResolveActorHandoffTarget(spotId)
                         ?? throw new ZLinkFrameworkException(
                             ZLinkFrameworkErrorKind.ActorRouteNotFound,
                             $"Actor '{request.ActorId}' handoff target '{spotId}' is not active during completion.");
            var actorRef = actorState.NativeActorRef
                           ?? throw new ZLinkFrameworkException(
                               ZLinkFrameworkErrorKind.ActorRouteNotFound,
                               $"Actor '{request.ActorId}' does not have a native Actor ref during route commit.");
            if (completionJournal is not null
                && (request.OperationIdHigh != 0 || request.OperationIdLow != 0)
                && completionRoot is null)
                completionRoot = await completionJournal.PrepareAsync(
                        request.ActorId,
                        new ZLinkActorJoinOperationId(
                            request.OperationIdHigh,
                            request.OperationIdLow),
                        actorRef.ToNative(),
                        request.ReplyContentType,
                        request.Reply ?? [],
                        cancellationToken)
                    .ConfigureAwait(false);
            await ReplayTransferredActorHandoffAsync(
                    target,
                    actorState,
                    request.Frames,
                    cancellationToken)
                .ConfigureAwait(false);
            await PublishTransferredActorLocationAsync(actorState, target, cancellationToken)
                .ConfigureAwait(false);
            if (completionRoot is
                {
                    Completion.Cursor: ZLinkDeferredJoinCompletionCursor.Prepared
                })
                completionRoot = await completionJournal!.MarkCommittedAsync(
                        completionRoot,
                        cancellationToken)
                    .ConfigureAwait(false);
            await ReplayFinalTransferredActorHandoffAsync(
                    target,
                    actorState,
                    cancellationToken)
                .ConfigureAwait(false);
            if (completionRoot is
                {
                    Completion.Cursor: ZLinkDeferredJoinCompletionCursor.Delivered
                })
            {
                // The callback succeeded before a crash or reply loss. The
                // durable cursor prevents an unnecessary duplicate attempt.
            }
            else if (request.OperationIdHigh != 0 || request.OperationIdLow != 0)
            {
                var actor = actorState.Actor
                            ?? throw new ZLinkFrameworkException(
                                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                                $"Actor '{request.ActorId}' has no transferred instance for Join completion.");
                var currentRef = actorState.NativeActorRef
                                 ?? throw new ZLinkFrameworkException(
                                   ZLinkFrameworkErrorKind.ActorRouteNotFound,
                                   $"Actor '{request.ActorId}' has no current reference for Join completion.");
                var reply = request.Reply is { Length: > 0 } payload
                            && request.ReplyContentType is { } contentType
                    ? ZLinkMessage.FromEncoded(contentType, payload, Registration.Codecs)
                    : null;
                await actorState.ExecuteRelocationCompletionAsync(
                        currentRef.Generation,
                        token => actor.OnJoinCompletedAsync(
                            new ZLinkActorJoinCompletion.Accepted(
                                new ZLinkActorJoinOperationId(
                                    request.OperationIdHigh,
                                    request.OperationIdLow),
                                currentRef.ToNative(),
                                reply),
                            token),
                        cancellationToken)
                    .ConfigureAwait(false);
                if (completionRoot is not null)
                    completionRoot = await completionJournal!.MarkDeliveredAsync(
                            completionRoot,
                            cancellationToken)
                        .ConfigureAwait(false);
            }
            actorState.Handoff.Complete(request.HandoffId);
            if (ownsRecordedCompletion)
            {
                _actorHandoffAdmissions.RecordCompletion(request, spotId);
                _actorHandoffAdmissions.Complete(request.HandoffId);
            }
            ZLinkFrameworkDebugLog.SpotDiscovery(
                $"handoff_completion actor={request.ActorId} id={request.HandoffId} frames={request.Frames.Count}");
        }
        catch
        {
            if (ownsRecordedCompletion)
                _actorHandoffAdmissions.CancelCompletion(request, spotId);
            throw;
        }
    }

    private ZLinkDeferredActorJoinCompletionJournal? CreateDeferredJoinCompletionJournal()
    {
        return Registration.Locations.ResolveStore() is IZLinkAuthorityStore authorityStore
               && Registration.Locations.RelocationStoreInstance is { } relocationStore
            ? new ZLinkDeferredActorJoinCompletionJournal(
                authorityStore,
                relocationStore)
            : null;
    }

    internal void ScheduleDeferredJoinCompletionRecovery(
        ZLinkActorRuntimeState actorState)
    {
        if (CreateDeferredJoinCompletionJournal() is null) return;
        _ = TryRunDetached(
            "actor-deferred-join-completion-recovery",
            token => ZLinkReconciliationRunner.RunAsync(
                retryToken => RecoverDeferredJoinCompletionAsync(
                    actorState,
                    retryToken),
                exception => ZLinkFrameworkDebugLog.SpotDiscovery(
                    $"deferred Join completion retry actor={actorState.ActorId}: {exception.Message}"),
                token,
                static exception => exception is OperationCanceledException));
    }

    private async ValueTask RecoverDeferredJoinCompletionAsync(
        ZLinkActorRuntimeState actorState,
        CancellationToken cancellationToken)
    {
        var journal = CreateDeferredJoinCompletionJournal();
        if (journal is null) return;
        var root = await journal.RecoverAsync(actorState.ActorId, cancellationToken)
            .ConfigureAwait(false);
        if (root is null
            || root.Completion.Cursor == ZLinkDeferredJoinCompletionCursor.Prepared)
            return;
        if (root.Completion.Cursor == ZLinkDeferredJoinCompletionCursor.Delivered)
            return;

        await actorState.ExecuteRelocationCompletionAsync(
                root.Completion.ObjectGeneration,
                async token =>
                {
                    // Another reconciliation attempt may have delivered while
                    // this mailbox turn was pending.
                    var current = await journal.RecoverAsync(
                            actorState.ActorId,
                            token)
                        .ConfigureAwait(false);
                    if (current is null) return;
                    if (current.Completion.Cursor
                        == ZLinkDeferredJoinCompletionCursor.Delivered)
                        return;
                    if (current.Completion.Cursor
                        != ZLinkDeferredJoinCompletionCursor.Committed)
                        return;

                    var actor = actorState.Actor
                                ?? throw new ZLinkFrameworkException(
                                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                                    $"Actor '{actorState.ActorId}' is not materialized for Join completion recovery.");
                    var reply = current.Completion.Reply.Length > 0
                                && current.Completion.ReplyContentType is { } contentType
                        ? ZLinkMessage.FromEncoded(
                            contentType,
                            current.Completion.Reply,
                            Registration.Codecs)
                        : null;
                    await actor.OnJoinCompletedAsync(
                            new ZLinkActorJoinCompletion.Accepted(
                                current.Completion.OperationId,
                                current.Completion.Actor,
                                reply),
                            token)
                        .ConfigureAwait(false);
                    current = await journal.MarkDeliveredAsync(current, token)
                        .ConfigureAwait(false);
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask ReconcileExpiredPreparedHandoffAsync(
        ZLinkActorRuntimeState actorState,
        ZLinkRemoteActorJoinRequest request,
        string spotId,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        await Task.Delay(timeout, cancellationToken).ConfigureAwait(false);
        var rejected = CreateRejectedHandoffReply(request.ActorId);
        while (!_actorHandoffAdmissions.TryExpirePreparedCommit(request, spotId, rejected))
        {
            if (!_actorHandoffAdmissions.IsPreparedCommitPending(request, spotId)) return;
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
        {
            _ = locations.SpotLocations.TryGetTrackedGeneration(
                userSpot.SpotId, out var spotGeneration);
            await locations.ActorOwnership.CommitTransferredActorLocationAsync(
                    actorState.ActorId,
                    actorRef.ToNative(),
                    userSpot.SpotId,
                    spotGeneration,
                    cancellationToken)
                .ConfigureAwait(false);
        }
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
        string spotId,
        ZLinkRemoteActorAdmissionRequest request,
        CancellationToken cancellationToken = default)
    {
        if (_drainAdmission.IsDraining)
            return ZLinkRemoteActorJoinPackets.CreateAdmissionReply(
                false,
                ZLinkMessage.Empty,
                Registration.Codecs,
                request.DeadlineUnixTimeMilliseconds);
        var target = ResolveActorHandoffTarget(spotId)
                     ?? throw new InvalidOperationException(
                         $"Actor handoff target '{spotId}' is not active.");

        return await _actorHandoffAdmissions.AdmitAsync(
                request,
                spotId,
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

    private ActorHandoffTarget? ResolveActorHandoffTarget(string spotId)
    {
        var state = _state
                    ?? throw new InvalidOperationException(
                        "ZLink framework runtime is not available for actor handoff.");
        if (_spots.GetActivationBySpotId(state, spotId) is { } userSpot)
            return new ActorHandoffTarget(
                spotId,
                userSpot.NodeRid,
                userSpot,
                null);
        var entryNode = state.SpotNodes.Values.FirstOrDefault(
            node => node.EntrySpotActivation is { } entrySpot
                    && string.Equals(entrySpot.SpotId, spotId, StringComparison.Ordinal));
        if (entryNode?.EntrySpotActivation is { } entrySpot)
            return new ActorHandoffTarget(
                spotId,
                entryNode.Node.RoutingId,
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
        await NotifyEntrySpotActorRelocatedAsync(
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
        string TargetRid,
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
            var response = await NotifyEntrySpotActorCreatedAsync(
                    result.Actor,
                    result.CreateRequest,
                    nativeRef.NodeRid,
                    cancellationToken)
                .ConfigureAwait(false);
            if (!response.Accepted)
            {
                await DestroyActorAsync(
                        nativeRef.NodeRid,
                        result.Actor,
                        CancellationToken.None)
                    .ConfigureAwait(false);
            }

            return result with { Response = response };
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

    internal async ValueTask<CreateActorResult> PrepareReservedActorAsync(
        string actorId,
        string actorType,
        ZLinkMessage createRequest,
        ulong objectGeneration,
        CancellationToken cancellationToken)
    {
        var result = await _actorSessionManager.PrepareReservedActorAsync(
                actorId,
                actorType,
                createRequest,
                objectGeneration,
                cancellationToken)
            .ConfigureAwait(false);
        if (!result.Created)
            return result;

        var state = GetOrCreateActorState(actorId);
        var nativeRef = state.NativeActorRef
                        ?? throw new ZLinkFrameworkException(
                            ZLinkFrameworkErrorKind.ActorRouteNotFound,
                            $"Actor '{actorId}' does not have a native Actor ref after reserved creation.");
        var response = await NotifyEntrySpotActorCreatedAsync(
                result.Actor,
                createRequest,
                nativeRef.NodeRid,
                cancellationToken)
            .ConfigureAwait(false);
        return result with { Response = response };
    }

    internal void PublishReservedActor(string actorId) =>
        _actorSessionManager.PublishReservedActor(actorId);

    internal ValueTask DiscardReservedActorAsync(
        string actorId,
        CancellationToken cancellationToken = default) =>
        _actorSessionManager.RollbackTransferredActorAsync(
            actorId,
            cancellationToken,
            startTeardownReconciliation: true);

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

    internal ValueTask NotifyActorDisconnectedAsync(
        ActorRef actor,
        string bindingToken,
        CancellationToken cancellationToken = default)
    {
        var nodeRuntime = GetActorClientSpotNodeRuntime();
        var meshName = nodeRuntime.Registration.SpotMeshChannelName
                       ?? nodeRuntime.Registration.SpotNodeName;
        return NotifyActorDisconnectedAsync(meshName, actor, bindingToken, cancellationToken);
    }

    internal async ValueTask NotifyActorDisconnectedAsync(
        string? meshName,
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
        {
            return;
        }

        if (state.Actor is not null
            && state.NativeActorRef is { } localActor
            && localActor.NodeRid == actor.NodeRid
            && localActor.Generation == actor.Generation)
        {
            await NotifyActorDisconnectedByIdAsync(actor.ActorId, cancellationToken)
                .ConfigureAwait(false);
            return;
        }

        var node = GetMeshNodeRuntime(
            meshName
            ?? throw new ZLinkConfigurationException(
                "STREAM Actor dispatch requires EnableActorDispatch(meshName).")).Node;

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

    internal ZLinkSessionBindingEntry[] BindSessionActor(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken,
        ZLinkSessionActor actorRef)
    {
        return _actorBoundSessionCoordinator.BindSessionActor(
            actorId,
            context,
            bindingToken,
            actorRef);
    }

    internal void UnbindSessionActor(
        string actorId,
        ZLinkSessionContext context,
        string bindingToken)
    {
        _actorBoundSessionCoordinator.UnbindSessionActor(actorId, context, bindingToken);
    }

    /// <summary>Actor-node entry for a relayed session frame whose bound actor
    /// migrated here: dispatches it through the standard actor inbound
    /// pipeline. The frame carries the session identity, so the dispatch
    /// binds the remote session route and replies travel back over the
    /// bound-session push relay.</summary>
    internal async ValueTask DispatchRemoteActorFrameAsync(
        string actorId,
        ulong actorGeneration,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        byte[] header,
        byte[] body,
        CancellationToken cancellationToken)
    {
        // The relay target is this node. Preserve the generation carried by
        // the incoming stale route instead of reading NativeActorRef: during
        // a chained transfer that state already points at the next owner, and
        // replacing the incoming identity would bypass this node's forwarding
        // mapping and attempt blocked local dispatch.
        var actorRef = new ZLinkBackendActorRef(
            GetActorClientSpotNode().RoutingId,
            actorId,
            actorGeneration);
        var parts = new[]
        {
            new ZLinkBackendActorPart(
                actorRef, sourceNodeRid, sourceSessionRid, 0, 0,
                Message.From(header), More: true),
            new ZLinkBackendActorPart(
                actorRef, sourceNodeRid, sourceSessionRid, 0, 0,
                Message.From(body), More: false)
        };
        var batch = ZLinkActorHandoffIngress.CaptureMovingFrames(this, parts);
        if (batch.Count == 0) return;
        // Per-actor FIFO across concurrently handled relay records: sibling
        // forwarded frames must not overtake each other (spec 23 §10.2).
        Task chained;
        lock (_remoteFrameChainGate)
        {
            var prior = _remoteFrameChains.TryGetValue(actorId, out var chain)
                ? chain
                : Task.CompletedTask;
            chained = DispatchRemoteFrameAfterAsync(prior, batch, cancellationToken);
            _remoteFrameChains[actorId] = chained;
        }

        try
        {
            await chained.ConfigureAwait(false);
        }
        finally
        {
            lock (_remoteFrameChainGate)
            {
                if (_remoteFrameChains.TryGetValue(actorId, out var current)
                    && ReferenceEquals(current, chained))
                    _remoteFrameChains.Remove(actorId);
            }
        }
    }

    private async Task DispatchRemoteFrameAfterAsync(
        Task prior,
        ZLinkSpotActorFrameBatch batch,
        CancellationToken cancellationToken)
    {
        try
        {
            await prior.ConfigureAwait(false);
        }
        catch
        {
            // The prior frame reported its own failure; the chain continues.
        }

        await new ZLinkActorInboundPipeline(this, new ZLinkEntrySpotActorInboundEndpoint(this))
            .DispatchAsync(batch, cancellationToken)
            .ConfigureAwait(false);
    }

    private readonly object _remoteFrameChainGate = new();
    private readonly Dictionary<string, Task> _remoteFrameChains = new(StringComparer.Ordinal);

    /// <summary>Session-node relay for a frame whose bound actor lives on
    /// another node: wraps the stream frame in the internal node-addressed
    /// actor-frame packet.</summary>
    private bool RelayRemoteActorFrame(
        string? meshName,
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        byte[] header,
        byte[] body)
    {
        var nodeRuntime = meshName is null
            ? GetActorClientSpotNodeRuntime()
            : GetMeshNodeRuntime(meshName);
        meshName = nodeRuntime.Registration.SpotMeshChannelName
                   ?? nodeRuntime.Registration.SpotNodeName;
        // A locally relayed frame carries no source node rid (the session is
        // on this node); the receiver needs the concrete session node for the
        // reply route, so substitute the local node rid.
        var sessionNodeRid = sourceNodeRid.IsEmpty ? nodeRuntime.Node.RoutingId : sourceNodeRid;
        // A caller-routed frame forwarded to a moved actor carries no session
        // identity; only the reply-route node rid is mandatory. The target's
        // dispatch binds nothing for an identity-less frame.
        if (sessionNodeRid.IsEmpty) return false;
        var relayMessage = new ZLinkRemoteActorFrameRelay(
            actor.ActorId,
            actor.Generation,
            sessionNodeRid.ToHex(),
            sourceSessionRid.ToHex(),
            header,
            body);
        var envelope = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            meshName,
            ZLinkRemoteActorFrameProtocol.PacketName);
        var parts = ZLinkEnvelopeCodec.EncodeParts(
            envelope,
            relayMessage,
            typeof(ZLinkRemoteActorFrameRelay),
            Registration.Codecs);
        var submit = nodeRuntime.Node.SendToNode(actor.NodeRid, parts, SendFlags.DontWait);
        ZLinkMessageParts.DisposeAll(parts);
        return submit == SubmitResult.Ok;
    }

    /// <summary>Session-node entry for a relayed remote push: delivers the
    /// encoded frame to the still-bound local session, retrying backpressured
    /// writes within the request timeout (a stale binding drops the push per
    /// spec 31 §6).</summary>
    internal async ValueTask DeliverRemoteSessionPushAsync(
        string actorId,
        RoutingId sessionRid,
        byte[] frame,
        CancellationToken cancellationToken)
    {
        var deadline = DateTime.UtcNow + Registration.DefaultRequestTimeout;
        while (true)
        {
            var delivery = _actorBoundSessionCoordinator.DeliverLocalSessionFrame(
                actorId, sessionRid, frame);
            // Backpressure and the transient release→bind gap of a rebind
            // both retry; a definite different-session binding drops the
            // push (spec 31 §6).
            var retryable = delivery
                is ZLinkActorBoundSessionCoordinator.RemotePushDelivery.Backpressured
                or ZLinkActorBoundSessionCoordinator.RemotePushDelivery.NoBinding;
            if (!retryable || DateTime.UtcNow >= deadline) return;
            await Task.Delay(TimeSpan.FromMilliseconds(10), cancellationToken)
                .ConfigureAwait(false);
        }
    }

    /// <summary>Actor-node relay for a push whose bound session lives on
    /// another node: wraps the frame in the internal node-addressed route
    /// packet. One-way push semantics — the submit runs on a detached runtime
    /// task so the actor's turn never blocks on route admission; failures are
    /// reported through the runtime task error sink.</summary>
    private bool RelayRemoteSessionPush(
        string actorId,
        RoutingId sessionNodeRid,
        RoutingId sessionRid,
        byte[] frame)
    {
        var nodeRuntime = GetActorClientSpotNodeRuntime();
        var meshName = nodeRuntime.Registration.SpotMeshChannelName
                       ?? nodeRuntime.Registration.SpotNodeName;
        var relayMessage = new ZLinkRemoteSessionPushRelay(actorId, sessionRid.ToHex(), frame);
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            meshName,
            ZLinkRemoteSessionPushProtocol.PacketName);
        var parts = ZLinkEnvelopeCodec.EncodeParts(
            header,
            relayMessage,
            typeof(ZLinkRemoteSessionPushRelay),
            Registration.Codecs);
        var submit = nodeRuntime.Node.SendToNode(sessionNodeRid, parts, SendFlags.DontWait);
        ZLinkMessageParts.DisposeAll(parts);
        return submit == SubmitResult.Ok;
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
        var nodeRuntime = GetActorClientSpotNodeRuntime();
        if (_actorBoundSessionCoordinator.ReplyNoBind(
                actor, sourceNodeRid, sourceSessionRid, requestId, flags, parts))
            return;

        if (!sourceNodeRid.IsEmpty
            && !sourceNodeRid.Equals(nodeRuntime.Node.RoutingId))
        {
            var frameLength = parts.Sum(static part => checked((int)part.Size));
            var frame = new byte[frameLength];
            var offset = 0;
            foreach (var part in parts)
            {
                part.AsReadOnlySpan().CopyTo(frame.AsSpan(offset));
                offset += checked((int)part.Size);
            }

            var meshName = nodeRuntime.Registration.SpotMeshChannelName
                           ?? nodeRuntime.Registration.SpotNodeName;
            var relay = new ZLinkRemoteActorReplyRelay(
                actor.ActorId,
                requestId,
                flags,
                frame);
            var envelope = ZLinkClientCallCodec.CreateEnvelope(
                ZLinkMessageKind.Command,
                meshName,
                ZLinkRemoteActorReplyProtocol.PacketName);
            var relayParts = ZLinkEnvelopeCodec.EncodeParts(
                envelope,
                relay,
                typeof(ZLinkRemoteActorReplyRelay),
                Registration.Codecs);
            var submit = nodeRuntime.Node.SendToNode(
                sourceNodeRid,
                relayParts,
                SendFlags.DontWait);
            ZLinkMessageParts.DisposeAll(relayParts);
            if (submit != SubmitResult.Ok)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RouteNotConnected,
                    $"Actor reply relay to node '{sourceNodeRid}' was not admitted.",
                    true);
            return;
        }

        ZLinkMessageParts.DisposeAll(parts);
    }

    internal void DeliverRemoteActorReply(
        string actorId,
        ulong requestId,
        uint flags,
        byte[] frame)
    {
        var message = Message.From(frame);
        if (!_actorBoundSessionCoordinator.ReplyNoBind(
                new ZLinkBackendActorRef(default, actorId, 0),
                default,
                default,
                requestId,
                flags,
                [message]))
            message.Dispose();
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
            actorRef, sourceNodeRid, sourceSessionRid, message, hasMore, flags);
    }

    internal bool ForwardActorBoundSessionPart(
        string meshName,
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
            flags,
            meshName,
            GetMeshNodeRuntime(meshName).Node);
    }

    internal ValueTask CloseActorBoundSessionAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        return _actorBoundSessionCoordinator.CloseAsync(actorId, cancellationToken);
    }
}
