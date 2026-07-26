using Microsoft.Extensions.DependencyInjection;

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
            || nativeRef.Generation != actor.ObjectGeneration)
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
        var authorityStore = Registration.Locations.ResolveStore()
                             ?? throw new ZLinkConfigurationException(
                                 "Cross-node Actor relocation requires an Authority Store.");
        var relocationStore = Registration.Locations.RelocationStoreInstance
                              ?? throw new ZLinkConfigurationException(
                                  "Cross-node Actor relocation requires a Relocation Store.");
        var target = ResolveActorHandoffTarget(spotId)
                     ?? throw new InvalidOperationException(
                         $"Actor handoff target '{spotId}' is not active.");
        await ValidateActorRelocationTargetAsync(
                request,
                target,
                authorityStore,
                cancellationToken)
            .ConfigureAwait(false);
        ZLinkRelocationEnvelope durableEnvelope;
        try
        {
            durableEnvelope =
                await new ZLinkRelocationPublicationCoordinator(
                        authorityStore,
                        relocationStore)
                    .ReadPreparedAsync(
                        ZLinkActorRelocationRoot.Reference(request),
                        cancellationToken)
                    .ConfigureAwait(false);
        }
        catch (ZLinkRelocationDataLostException error)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RelocationDataLost,
                error.Message,
                isRetriable: false,
                error);
        }
        var durable = ZLinkActorRelocationRoot.Load(
            request,
            durableEnvelope);
        var currentAuthority = await authorityStore.ReadAuthorityAsync(
                ZLinkActorAuthorityPayloadCodec.AuthorityKey(request.ActorId),
                cancellationToken)
            .ConfigureAwait(false);
        if (currentAuthority is ZLinkAuthorityReadResult.Found current
            && ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                current.Snapshot.Payload.Span,
                out var currentPublication)
            && (!string.Equals(
                    currentPublication.Reference,
                    request.RelocationReference,
                    StringComparison.Ordinal)
                || currentPublication.ChecksumCrc32c
                != request.RelocationChecksumCrc32c))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RelocationDataLost,
                $"Actor '{request.ActorId}' authority references another relocation root.",
                isRetriable: false);
        request = ZLinkActorRelocationRoot.WithDurableFrames(
            request,
            durable);
        var inboundPayloadBytes =
            ZLinkRemoteActorJoinPackets.MeasureRelocationPayloadBytes(request);
        inboundPayloadBytes = checked(
            inboundPayloadBytes
            + ZLinkRelocationEnvelopeCodec.MeasureEncodedLength(durableEnvelope));
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

        ZLinkActorRelocationRegistry.TryResolve(
            Registration,
            request.ActorType,
            target.NodeRid,
            out var relocation);
        if (relocation is null)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestRejected,
                $"Actor type '{request.ActorType}' relocation policy is not registered on the target node.");
        ZLinkRelocationPermitPool.ZLinkRelocationPermitLease relocationPermit = default;
        using (relocationPermit)
        {
            var ownsImport = false;
            var createdTransferredActor = false;
            var authorityCommitted = actorState.Handoff.IsAuthorityCommitted(request.HandoffId);
            try
            {
                if (!actorState.Handoff.IsKnown(request.HandoffId))
                    _actorHandoffAdmissions.BeginCommit(
                        request,
                        spotId,
                        inboundPayloadBytes);
                var import = await actorState.ExecuteHandoffTransitionAsync(
                        () =>
                        {
                            var owned = actorState.Handoff.Import(request, out var preparation);
                            return (Owned: owned, Preparation: preparation);
                        },
                        cancellationToken)
                    .ConfigureAwait(false);
                if (!import.Owned)
                {
                    if (!actorState.Handoff.IsAuthorityCommitted(request.HandoffId))
                        return await import.Preparation.WaitAsync(cancellationToken).ConfigureAwait(false);
                    return await CompleteCommittedActorJoinTargetAsync(
                            target,
                            actorState,
                            request,
                            spotId,
                            import.Preparation,
                            cancellationToken)
                        .ConfigureAwait(false);
                }
                ownsImport = true;

                await _actorSessionManager.PrepareForTransferredActivationAsync(
                        actorState,
                        cancellationToken)
                    .ConfigureAwait(false);
                // Hosting handoff: the source node still owns the location row, so
                // the local claim may fence it out with Takeover. This path does not
                // call the Entry Spot create callback; transfer materialization is
                // not a new application-level actor creation.
                var creation = await _actorSessionManager.RelocateAndBindActorAsync(
                        request.ActorId,
                        request.ActorType,
                        relocation,
                        ZLinkActorRelocationRegistry.ValidateIncomingPayload(
                            relocation,
                            request.ActorType,
                            request.RelocationContentType,
                            durable.Participant.ApplicationState),
                        request.ActorGeneration,
                        request.ActorAuthorityOwnerGeneration,
                        ZLinkActorClaimMode.StagedRelocation,
                        publishActorRef: false,
                        cancellationToken)
                    .ConfigureAwait(false);
                createdTransferredActor = creation.Created;
                var actorId = request.ActorId;
                var actorRef = actorState.NativeActorRef
                               ?? throw new ZLinkFrameworkException(
                                   ZLinkFrameworkErrorKind.ActorRouteNotFound,
                                   $"Actor '{actorId}' does not have a native Actor ref.");
                if (actorRef.Generation != request.ActorGeneration)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorGenerationStale,
                        $"Actor '{actorId}' target generation changed during handoff.");
                var boundRoute = ZLinkRemoteActorJoinPackets.DecodeBoundSessionRoute(request);
                if (boundRoute.HasRouteCoordinates && !boundRoute.IsBound)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.RequestProtocolError,
                        $"Actor '{actorId}' relocation session route has incomplete fencing identity.");
                actorState.StageRelocationSessionRoute(
                    request.HandoffId,
                    boundRoute);
                await PrepareTransferredActorTargetAsync(
                        target,
                        creation.Actor,
                        actorState,
                        cancellationToken)
                    .ConfigureAwait(false);
                // The authority CAS is the visibility boundary. The target
                // lifecycle callback is retryable post-commit work and cannot
                // turn the move back into a source-side rejection.
                var committedAuthority =
                    await PublishTransferredActorAuthorityAsync(
                        actorState,
                        target,
                        request.HandoffId,
                        request.ActorGeneration,
                        ZLinkActorRelocationRoot.Reference(request),
                        cancellationToken)
                    .ConfigureAwait(false);
                actorState.MarkRelocationSessionAuthorityCommitted(
                    request.HandoffId,
                    actorRef,
                    committedAuthority.AuthorityOwnerGeneration,
                    committedAuthority.MeshName,
                    committedAuthority.NodeGeneration,
                    committedAuthority.OwnerLeaseGeneration);
                authorityCommitted = true;
                return await CompleteCommittedActorJoinTargetAsync(
                        target,
                        actorState,
                        request,
                        spotId,
                        import.Preparation,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
            catch (Exception commitFailure)
            {
                if (authorityCommitted
                    || actorState.Handoff.IsAuthorityCommitted(request.HandoffId))
                    throw;

                var rejected = CreateRejectedHandoffReply(request.ActorId);
                _actorHandoffAdmissions.RejectPreparedJoinOutcome(request, spotId, rejected);
                if (ownsImport)
                    actorState.Handoff.RejectPreparation(request.HandoffId, rejected);
                actorState.AbortRelocationSessionRoute(request.HandoffId);
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
    }

    private async ValueTask<ZLinkRemoteActorJoinReply> CompleteCommittedActorJoinTargetAsync(
        ActorHandoffTarget target,
        ZLinkActorRuntimeState actorState,
        ZLinkRemoteActorJoinRequest request,
        string spotId,
        Task<ZLinkRemoteActorJoinReply> preparation,
        CancellationToken cancellationToken)
    {
        if (!actorState.Handoff.TryBeginJoinedNotification(request.HandoffId))
            return await preparation.WaitAsync(cancellationToken).ConfigureAwait(false);

        try
        {
            await CompleteTransferredActorTargetAsync(
                    target,
                    actorState,
                    cancellationToken)
                .ConfigureAwait(false);
            var actorRef = actorState.NativeActorRef
                           ?? throw new ZLinkFrameworkException(
                               ZLinkFrameworkErrorKind.ActorRouteNotFound,
                               $"Actor '{request.ActorId}' does not have a native Actor ref.");
            var reply = ZLinkRemoteActorJoinPackets.CreateJoinReply(true, actorRef);
            _actorHandoffAdmissions.RecordJoinOutcome(
                request,
                spotId,
                reply,
                Registration.DefaultRequestTimeout);
            actorState.Handoff.AcceptPreparation(request.HandoffId, reply);
            return reply;
        }
        catch
        {
            actorState.Handoff.RetryJoinedNotification(request.HandoffId);
            throw;
        }
    }

    internal async ValueTask CompleteRoutedActorHandoffAsync(
        string spotId,
        ZLinkRemoteActorHandoffCompletionRequest request,
        CancellationToken cancellationToken)
    {
        var authorityStore = Registration.Locations.ResolveStore()
                             ?? throw new ZLinkConfigurationException(
                                 "Actor relocation completion requires an Authority Store.");
        var relocationStore = Registration.Locations.RelocationStoreInstance
                              ?? throw new ZLinkConfigurationException(
                                  "Actor relocation completion requires a Relocation Store.");
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
        var publishedRead = await authorityStore.ReadAuthorityAsync(
                ZLinkActorAuthorityPayloadCodec.AuthorityKey(request.ActorId),
                cancellationToken)
            .ConfigureAwait(false);
        if (publishedRead is not ZLinkAuthorityReadResult.Found publishedFound
            || !ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                publishedFound.Snapshot.Payload.Span,
                out var publishedManifest))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RelocationDataLost,
                $"Actor '{request.ActorId}' relocation reference is not published.",
                isRetriable: false);
        var publishedReference = publishedManifest.Reference;
        if (!ZLinkActorAuthorityPayloadCodec.TryDecodeRelocating(
                publishedManifest.ApplicationPayload.Span,
                out var publishedActorAuthority))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RelocationDataLost,
                $"Actor '{request.ActorId}' published authority identity is unreadable.",
                isRetriable: false);
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
            var relocationId = Guid.ParseExact(request.HandoffId, "N");
            var actorLocations = RequireActorRelocationLocationLifecycle(
                    LocationLifecycle,
                    request.ActorId)
                .ActorOwnership;
            var durablePhase = await actorLocations
                .ReadTransferredActorAuthorityPhaseAsync(
                    request.ActorId,
                    actorRef.ToNative(publishedActorAuthority.MeshName),
                    cancellationToken)
                .ConfigureAwait(false);
            var authorityWasNormalized = false;
            ZLinkActorRelocationAuthorityPayload? durablePayload = null;
            if (durablePhase is { } durable)
            {
                durablePayload = durable.Phase;
                if (durable.Phase.RelocationId != relocationId
                    || durable.Phase.Phase
                    is < ZLinkActorRelocationAuthorityPhase.Completed)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorMoving,
                        $"Actor '{request.ActorId}' source cleanup has not reached durable Completed.");
            }
            else
            {
                var normalizedRead = await authorityStore.ReadAuthorityAsync(
                        ZLinkActorAuthorityPayloadCodec.AuthorityKey(
                            request.ActorId),
                        cancellationToken)
                    .ConfigureAwait(false);
                authorityWasNormalized =
                    normalizedRead is ZLinkAuthorityReadResult.Found normalized
                    && ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                        normalized.Snapshot.Payload.Span,
                        out var normalizedPublication)
                    && string.Equals(
                        normalizedPublication.Reference,
                        publishedReference,
                        StringComparison.Ordinal)
                    && ZLinkActorAuthorityPayloadCodec.TryDecode(
                        normalizedPublication.ApplicationPayload.Span,
                        out _);
                if (!authorityWasNormalized)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorMoving,
                        $"Actor '{request.ActorId}' source cleanup has not reached durable Completed.");
            }
            var authorityWasSteady = authorityWasNormalized
                                     || durablePayload?.Phase
                                     == ZLinkActorRelocationAuthorityPhase.Steady;
            var recoveryBoundRoute =
                durablePayload?.BoundSessionRoute.IsBound == true
                    ? durablePayload.BoundSessionRoute
                    : ZLinkRemoteActorJoinPackets.DecodeBoundSessionRoute(
                        request);
            if (recoveryBoundRoute.IsBound
                && !actorState.TryGetStagedRelocationSessionRoute(
                    request.HandoffId,
                    out _))
            {
                var committedAuthority = publishedActorAuthority;
                if (durablePayload is not null
                    && !ZLinkActorAuthorityPayloadCodec.TryDecodeRelocating(
                        durablePayload.ApplicationPayload.Span,
                        out committedAuthority))
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.RelocationDataLost,
                        $"Actor '{request.ActorId}' committed authority identity is unreadable.",
                        isRetriable: false);
                actorState.StageRelocationSessionRoute(
                    request.HandoffId,
                    recoveryBoundRoute);
                actorState.MarkRelocationSessionAuthorityCommitted(
                    request.HandoffId,
                    actorRef,
                    publishedFound.Snapshot.AuthorityOwnerGeneration,
                    committedAuthority.MeshName,
                    committedAuthority.NodeGeneration,
                    committedAuthority.OwnerLeaseGeneration);
            }
            if (completionJournal is not null
                && (request.OperationIdHigh != 0 || request.OperationIdLow != 0)
                && completionRoot is null)
                completionRoot = await completionJournal.PrepareAsync(
                        request.ActorId,
                        new ZLinkActorJoinOperationId(
                            request.OperationIdHigh,
                            request.OperationIdLow),
                        actorRef.ToNative(publishedActorAuthority.MeshName),
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
                                currentRef.ToNative(publishedActorAuthority.MeshName),
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
            // Reconcile command 44/45 even when authority is already Steady
            // or normalized. The exact idempotent ACK proves that the session
            // owner switched this binding; a prior crash may have happened
            // after authority publication but before route switch or unseal.
            var sessionRouteCommit =
                await CommitCompletedSessionRouteAsync(
                        actorState,
                        request.HandoffId,
                        cancellationToken)
                    .ConfigureAwait(false);
            if (!authorityWasSteady)
                await actorLocations.AdvanceTransferredActorAuthorityPhaseAsync(
                        request.ActorId,
                        actorRef.ToNative(publishedActorAuthority.MeshName),
                        relocationId,
                        ZLinkActorRelocationAuthorityPhase.Completed,
                        ZLinkActorRelocationAuthorityPhase.Steady,
                        cancellationToken)
                    .ConfigureAwait(false);
            if (!authorityWasNormalized)
                await actorLocations.NormalizeTransferredActorAuthorityAsync(
                        request.ActorId,
                        actorRef.ToNative(publishedActorAuthority.MeshName),
                        relocationId,
                        cancellationToken)
                    .ConfigureAwait(false);
            if (sessionRouteCommit is not null)
            {
                actorState.CompleteRelocationSessionRoute(request.HandoffId);
                await UnsealCompletedSessionRouteAsync(
                        sessionRouteCommit,
                        cancellationToken)
                    .ConfigureAwait(false);
            }
            actorState.Handoff.Complete(request.HandoffId);
            if (completionRoot is
                {
                    Completion.Cursor: ZLinkDeferredJoinCompletionCursor.Delivered
                })
                await completionJournal!.ReleaseAsync(
                        completionRoot,
                        cancellationToken)
                    .ConfigureAwait(false);
            else if (completionRoot is null)
                await new ZLinkRelocationPublicationCoordinator(
                        authorityStore,
                        relocationStore)
                    .ReleasePublishedAsync(
                        ZLinkActorAuthorityPayloadCodec.AuthorityKey(
                            request.ActorId),
                        publishedReference,
                        cancellationToken)
                    .ConfigureAwait(false);
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

    private async ValueTask<ZLinkSessionRouteCommitRequest?>
        CommitCompletedSessionRouteAsync(
        ZLinkActorRuntimeState actorState,
        string handoffId,
        CancellationToken cancellationToken)
    {
        if (!actorState.TryGetCommittedRelocationSessionRoute(
                handoffId,
                out var pending))
            return null;

        var route = pending.Route;
        var targetActor = pending.TargetActor
                          ?? throw new InvalidOperationException(
                              "Session route commit requires a target Actor ref.");
        var request = new ZLinkSessionRouteCommitRequest(
            actorState.ActorId,
            route.BindingToken!,
            route.BindingGeneration,
            route.ObjectGeneration,
            route.AuthorityOwnerGeneration,
            pending.TargetAuthorityOwnerGeneration,
            route.MeshName!,
            pending.TargetMeshName!,
            route.TargetNodeGeneration,
            pending.TargetNodeGeneration,
            route.OwnerLeaseGeneration,
            pending.TargetOwnerLeaseGeneration,
            route.SessionOwnerNodeGeneration,
            route.AcceptedHighWater,
            handoffId,
            targetActor.NodeRid.ToHex());

        var meshName = route.MeshName
                       ?? throw new ZLinkFrameworkException(
                           ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorState.ActorId}' session route has no Mesh.");
        var sessionOwnerNode = route.NodeRid!.Value;
        var localNode = GetMeshNodeRuntime(meshName).Node.RoutingId;
        ZLinkSessionRouteCommitReply reply;
        if (sessionOwnerNode == localNode)
        {
            var result = CommitSessionActorRoute(
                new ZLinkSessionRouteCommit(
                    request.ActorId,
                    request.BindingToken,
                    request.BindingGeneration,
                    request.ObjectGeneration,
                    request.PreviousAuthorityOwnerGeneration,
                    request.TargetAuthorityOwnerGeneration,
                    request.PreviousMeshName,
                    request.TargetMeshName,
                    request.PreviousTargetNodeGeneration,
                    request.TargetNodeGeneration,
                    request.PreviousOwnerLeaseGeneration,
                    request.TargetOwnerLeaseGeneration,
                    request.SessionOwnerNodeGeneration,
                    request.AcceptedHighWater,
                    request.HandoffId,
                    targetActor.ToNative(request.TargetMeshName)));
            reply = new ZLinkSessionRouteCommitReply(
                result.Acknowledged,
                result.AcceptedHighWater);
        }
        else
        {
            var routeClient =
                (IZLinkRouteClient?)Services.GetService(typeof(IZLinkRouteClient))
                ?? throw new InvalidOperationException(
                    "Route client service is unavailable during session route commit.");
            reply = await routeClient
                .RequestToNode(meshName, sessionOwnerNode, request)
                .Timeout(Registration.DefaultRequestTimeout)
                .Async<ZLinkSessionRouteCommitReply>(cancellationToken)
                .ConfigureAwait(false);
        }

        if (!reply.Acknowledged
            || reply.AcceptedHighWater != route.AcceptedHighWater)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorSessionNotBound,
                $"Actor '{actorState.ActorId}' session route commit was fenced by its binding identity.");

        return request;
    }

    private async ValueTask UnsealCompletedSessionRouteAsync(
        ZLinkSessionRouteCommitRequest request,
        CancellationToken cancellationToken)
    {
        var meshName = request.PreviousMeshName;
        var phase = await RequireActorRelocationLocationLifecycle(
                LocationLifecycle,
                request.ActorId)
            .ActorOwnership
            .ReadTransferredActorAuthorityPhaseAsync(
                request.ActorId,
                new ActorRef(
                    request.ActorId,
                    request.ObjectGeneration,
                    request.TargetMeshName,
                    RoutingId.FromHex(request.TargetNodeRid)),
                cancellationToken)
            .ConfigureAwait(false);
        if (phase is not { } durable
            || durable.Phase.RelocationId
            != Guid.ParseExact(request.HandoffId, "N")
            || durable.Phase.Phase != ZLinkActorRelocationAuthorityPhase.Steady
            || durable.Phase.BoundSessionRoute.NodeRid is not { } sessionNode)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorSessionNotBound,
                $"Actor '{request.ActorId}' steady authority lost its session owner.");
        var localNode = GetMeshNodeRuntime(meshName).Node.RoutingId;
        bool acknowledged;
        if (sessionNode == localNode)
        {
            acknowledged = UnsealCommittedSessionActorRoute(
                new ZLinkSessionRouteCommit(
                    request.ActorId,
                    request.BindingToken,
                    request.BindingGeneration,
                    request.ObjectGeneration,
                    request.PreviousAuthorityOwnerGeneration,
                    request.TargetAuthorityOwnerGeneration,
                    request.PreviousMeshName,
                    request.TargetMeshName,
                    request.PreviousTargetNodeGeneration,
                    request.TargetNodeGeneration,
                    request.PreviousOwnerLeaseGeneration,
                    request.TargetOwnerLeaseGeneration,
                    request.SessionOwnerNodeGeneration,
                    request.AcceptedHighWater,
                    request.HandoffId,
                    new ActorRef(
                        request.ActorId,
                        request.ObjectGeneration,
                        request.TargetMeshName,
                        RoutingId.FromHex(request.TargetNodeRid))));
        }
        else
        {
            var routeClient =
                (IZLinkRouteClient?)Services.GetService(typeof(IZLinkRouteClient))
                ?? throw new InvalidOperationException(
                    "Route client service is unavailable during session route unseal.");
            var reply = await routeClient
                .RequestToNode(
                    meshName,
                    sessionNode,
                    new ZLinkSessionRouteUnsealRequest(
                        request.ActorId,
                        request.BindingToken,
                        request.BindingGeneration,
                        request.ObjectGeneration,
                        request.PreviousAuthorityOwnerGeneration,
                        request.TargetAuthorityOwnerGeneration,
                        request.PreviousMeshName,
                        request.TargetMeshName,
                        request.PreviousTargetNodeGeneration,
                        request.TargetNodeGeneration,
                        request.PreviousOwnerLeaseGeneration,
                        request.TargetOwnerLeaseGeneration,
                        request.SessionOwnerNodeGeneration,
                        request.AcceptedHighWater,
                        request.HandoffId,
                        request.TargetNodeRid))
                .Timeout(Registration.DefaultRequestTimeout)
                .Async<ZLinkSessionRouteCommitReply>(cancellationToken)
                .ConfigureAwait(false);
            acknowledged = reply.Acknowledged;
        }
        if (!acknowledged)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorSessionNotBound,
                $"Actor '{request.ActorId}' session ingress could not unseal after steady normalization.");
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

    internal async ValueTask RecoverPublishedRelocationsAsync(
        CancellationToken cancellationToken)
    {
        if (Registration.Locations.ResolveStore()
                is not IZLinkAuthorityStore authorityStore
            || Registration.Locations.RelocationStoreInstance
                is not { } relocationStore)
            return;
        await new ZLinkRelocationStartupRecovery(
                authorityStore,
                relocationStore)
            .RecoverAsync(
                async (candidate, token) =>
                {
                    if (candidate.Authorities.Any(
                            static entry =>
                                entry.Snapshot.Allocation.ObjectKind
                                is ZLinkPlacementObjectKind.UserSpot
                                or ZLinkPlacementObjectKind.InstanceSpot))
                    {
                        if (Services.GetService(
                                typeof(ZLinkSpotRetireTargetRuntime))
                            is ZLinkSpotRetireTargetRuntime spotRecovery)
                            await spotRecovery.RecoverPublishedAsync(
                                    candidate,
                                    token)
                                .ConfigureAwait(false);
                        return;
                    }
                    var actorAuthority = candidate.Authorities.SingleOrDefault(
                        static entry => entry.Snapshot.Allocation.ObjectKind
                                        == ZLinkPlacementObjectKind.Actor);
                    if (actorAuthority is null)
                        return; // User Spot aggregate recovery is owned by its target bridge.
                    if (candidate.Authorities.Count != 1)
                        throw new ZLinkFrameworkException(
                            ZLinkFrameworkErrorKind.RelocationDataLost,
                            "Standalone Actor relocation root contains another authority.",
                            isRetriable: false);
                    if (!ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                            actorAuthority.Snapshot.Payload.Span,
                            out var publication)
                        || !ZLinkActorAuthorityPayloadCodec.TryDecodeRelocating(
                            publication.ApplicationPayload.Span,
                            out var actorPayload))
                        throw new ZLinkFrameworkException(
                            ZLinkFrameworkErrorKind.RelocationDataLost,
                            $"Actor authority '{actorAuthority.Key.Value}' has an invalid relocation payload.",
                            isRetriable: false);
                    var hasPhase =
                        ZLinkActorRelocationAuthorityPayloadCodec.TryDecode(
                            publication.ApplicationPayload.Span,
                            out _);
                    var localNode = _state?.SpotNodes.Values.SingleOrDefault(
                        node => node.Node.RoutingId == actorPayload.NodeRid);
                    if (localNode is null
                        || actorPayload.NodeGeneration
                        != localNode.Node.MeshStatus().LifecycleGeneration)
                        return;

                    var templateParticipant =
                        candidate.Envelope.Participants.Single();
                    ZLinkActorRelocationRecoveryRecord recovery;
                    try
                    {
                        recovery = System.Text.Json.JsonSerializer.Deserialize<
                                       ZLinkActorRelocationRecoveryRecord>(
                                       templateParticipant.RecoveryPayload.Span)
                                   ?? throw new System.Text.Json.JsonException();
                    }
                    catch (Exception error) when (error
                                                  is System.Text.Json.JsonException
                                                  or NotSupportedException)
                    {
                        throw new ZLinkFrameworkException(
                            ZLinkFrameworkErrorKind.RelocationDataLost,
                            "Actor relocation recovery metadata is malformed.",
                            isRetriable: false,
                            error);
                    }
                    var wire = recovery.Request with
                    {
                        RelocationReference =
                            candidate.Reference.Reference,
                        RelocationChecksumCrc32c =
                            candidate.Reference.ChecksumCrc32c,
                        RelocationAggregateId =
                            candidate.Reference.AggregateId,
                        RelocationAggregateGeneration =
                            candidate.Reference.AggregateGeneration,
                        RelocationInventoryDigest =
                            candidate.Reference.InventoryDigest.ToArray(),
                        HandoffFrames = []
                    };
                    if (!recovery.TargetNodeRid.AsSpan().SequenceEqual(
                            actorPayload.NodeRid.ToBytes())
                        || recovery.TargetNodeGeneration
                           != actorPayload.NodeGeneration
                        || recovery.TargetSpotGeneration
                           != actorPayload.CurrentSpotGeneration
                        || recovery.TargetAuthorityOwnerGeneration
                           != actorAuthority.Snapshot.AuthorityOwnerGeneration)
                        throw new ZLinkFrameworkException(
                            ZLinkFrameworkErrorKind.RelocationDataLost,
                            $"Actor '{wire.ActorId}' durable target fence does not match its published authority.",
                            isRetriable: false);
                    ZLinkActorRelocationRegistry.TryResolve(
                        Registration,
                        wire.ActorType,
                        actorPayload.NodeRid,
                        out var recoveredRelocation);
                    if (recoveredRelocation is null
                        || !_relocationPermits.TryAcquire(
                            ZLinkRelocationPermitRequest.Inbound(
                                wire.ReservedPayloadBytes,
                                restore: recoveredRelocation.PolicyKind == 2),
                            out var recoveredReservation))
                        throw new ZLinkFrameworkException(
                            ZLinkFrameworkErrorKind.ActorMoving,
                            $"Actor '{wire.ActorId}' recovery target reservation is busy.");
                    try
                    {
                        _actorHandoffAdmissions.RegisterRecoveredReservation(
                            wire,
                            recovery.TargetSpotId,
                            DateTimeOffset.UtcNow + Registration.DefaultRequestTimeout,
                            recoveredReservation);
                    }
                    catch
                    {
                        recoveredReservation.Dispose();
                        throw;
                    }
                    await JoinRoutedActorAsync(
                            recovery.TargetSpotId,
                            wire,
                            token)
                        .ConfigureAwait(false);
                    var actorRef = GetOrCreateActorState(wire.ActorId)
                                       .NativeActorRef
                                   ?? throw new ZLinkFrameworkException(
                                       ZLinkFrameworkErrorKind.ActorRouteNotFound,
                                       $"Actor '{wire.ActorId}' was not restored during relocation recovery.");
                    var ownership = RequireActorRelocationLocationLifecycle(
                            LocationLifecycle,
                            wire.ActorId)
                        .ActorOwnership;
                    var durable = await ownership
                        .ReadTransferredActorAuthorityPhaseAsync(
                            wire.ActorId,
                            actorRef.ToNative(actorPayload.MeshName),
                            token)
                        .ConfigureAwait(false);
                    if (hasPhase
                        && (durable is null
                            || durable.Value.Phase.RelocationId
                            != candidate.Reference.AggregateId))
                        throw new ZLinkFrameworkException(
                            ZLinkFrameworkErrorKind.RelocationDataLost,
                            $"Actor '{wire.ActorId}' lost its committed relocation phase.",
                            isRetriable: false);
                    if (durable?.Phase.Phase
                        == ZLinkActorRelocationAuthorityPhase.Activated)
                        await ownership.AdvanceTransferredActorAuthorityPhaseAsync(
                                wire.ActorId,
                                actorRef.ToNative(actorPayload.MeshName),
                                candidate.Reference.AggregateId,
                                ZLinkActorRelocationAuthorityPhase.Activated,
                                ZLinkActorRelocationAuthorityPhase.Cleaning,
                                token)
                            .ConfigureAwait(false);
                    if (hasPhase)
                    {
                        durable = await ownership
                            .ReadTransferredActorAuthorityPhaseAsync(
                            wire.ActorId,
                            actorRef.ToNative(actorPayload.MeshName),
                                token)
                            .ConfigureAwait(false);
                        if (durable?.Phase.Phase
                            == ZLinkActorRelocationAuthorityPhase.Cleaning)
                            await ownership.AdvanceTransferredActorAuthorityPhaseAsync(
                                    wire.ActorId,
                                    actorRef.ToNative(actorPayload.MeshName),
                                    candidate.Reference.AggregateId,
                                    ZLinkActorRelocationAuthorityPhase.Cleaning,
                                    ZLinkActorRelocationAuthorityPhase.Completed,
                                    token)
                                .ConfigureAwait(false);
                    }
                    await CompleteRoutedActorHandoffAsync(
                            recovery.TargetSpotId,
                            new ZLinkRemoteActorHandoffCompletionRequest(
                                wire.ActorId,
                                wire.HandoffId,
                                wire.SourceSpotId,
                                wire.SourceNodeRid,
                                recovery.TargetSpotId,
                                [],
                                recovery.OperationIdHigh,
                                recovery.OperationIdLow,
                                recovery.ReplyContentType,
                                recovery.Reply,
                                wire.BoundSessionNodeRid,
                                wire.BoundSessionRid,
                                wire.BoundSessionBindingToken,
                                wire.BoundSessionBindingGeneration,
                                wire.BoundSessionObjectGeneration,
                                wire.BoundSessionAuthorityOwnerGeneration,
                                wire.BoundSessionMeshName,
                                wire.BoundSessionTargetNodeGeneration,
                                wire.BoundSessionOwnerLeaseGeneration,
                                wire.BoundSessionOwnerNodeGeneration,
                                wire.BoundSessionAcceptedHighWater),
                            token)
                        .ConfigureAwait(false);
                },
                cancellationToken)
            .ConfigureAwait(false);
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

    private async ValueTask<ZLinkCommittedActorAuthority>
        PublishTransferredActorAuthorityAsync(
        ZLinkActorRuntimeState actorState,
        ActorHandoffTarget target,
        string handoffId,
        ulong sourceObjectGeneration,
        ZLinkRelocationManifestReference relocationReference,
        CancellationToken cancellationToken)
    {
        var locations = RequireActorRelocationLocationLifecycle(
            LocationLifecycle,
            actorState.ActorId);

        var actorRef = actorState.NativeActorRef
                       ?? throw new ZLinkFrameworkException(
                           ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorState.ActorId}' does not have a native Actor ref during location commit.");
        var targetSpotId = target.UserSpot?.SpotId
                           ?? target.EntrySpot?.SpotId
                           ?? throw new InvalidOperationException(
                               $"Actor '{actorState.ActorId}' handoff target has no Spot identity.");
        if (!locations.SpotLocations.TryGetTrackedGeneration(
                targetSpotId,
                out var targetSpotGeneration)
            || targetSpotGeneration == 0)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorState.ActorId}' handoff target Spot generation is unavailable.");

        var actorType = actorState.ActorType
                        ?? throw new InvalidOperationException(
                            $"Actor '{actorState.ActorId}' has no registered type during handoff.");
        var meshName = ResolveActorDrainMeshName(Registration, actorType)
                       ?? throw new InvalidOperationException(
                           $"Actor '{actorState.ActorId}' has no mesh during handoff.");
        var committedAuthority =
            await locations.ActorOwnership.CommitTransferredActorAuthorityAsync(
                    actorState.ActorId,
                    actorRef.ToNative(meshName),
                    meshName,
                    targetSpotId,
                    targetSpotGeneration,
                    target.UserSpot is null
                        ? ZLinkSpotKind.Entry
                        : ZLinkSpotKind.User,
                    Guid.ParseExact(handoffId, "N"),
                    actorState.TryGetStagedRelocationSessionRoute(
                        handoffId,
                        out var stagedSessionRoute)
                        ? stagedSessionRoute
                        : default,
                    relocationReference,
                    _ => DeactivateActorOnOwnershipLossAsync(actorState.ActorId),
                    cancellationToken)
                .ConfigureAwait(false);
        actorState.Handoff.MarkAuthorityCommitted(
            handoffId,
            sourceObjectGeneration,
            actorRef.Generation);
        GetMeshNodeRuntime(meshName).Node.SetLocalActorAuthority(
            actorRef,
            committedAuthority.AuthorityOwnerGeneration);
        LogActorHandoff(
            $"location_committed actor={actorState.ActorId} spot={target.TargetRid}");
        return committedAuthority;
    }

    internal static ZLinkLocationLifecycle RequireActorRelocationLocationLifecycle(
        ZLinkLocationLifecycle? lifecycle,
        string actorId)
        => lifecycle
           ?? throw new ZLinkFrameworkException(
               ZLinkFrameworkErrorKind.ActorRouteNotFound,
               $"Actor '{actorId}' relocation cannot publish authority because the Location runtime is unavailable.");

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

        return await _actorHandoffAdmissions.AdmitReservedAsync(
                request,
                spotId,
                async ct =>
                {
                    var targetSpotGeneration = target.UserSpot?.ObjectGeneration
                                               ?? target.EntrySpot?.ObjectGeneration
                                               ?? 0;
                    var targetNodeGeneration = GetSpotNodeRuntime(target.NodeRid)
                        .Node
                        .MeshStatus()
                        .LifecycleGeneration;
                    var expectedTargetAuthorityOwnerGeneration = checked(
                        request.ActorAuthorityOwnerGeneration + 1);
                    var authorityStore = Registration.Locations.ResolveStore()
                                         ?? throw new ZLinkConfigurationException(
                                             "Cross-node Actor relocation requires an Authority Store.");
                    var targetAuthority = await authorityStore.ReadAuthorityAsync(
                            ZLinkUserSpotAuthorityPayloadCodec.AuthorityKey(
                                target.TargetRid),
                            ct)
                        .ConfigureAwait(false);
                    if (request.ActorGeneration == 0
                        || request.ActorAuthorityOwnerGeneration == 0
                        || request.PredictedPayloadBytes <= 0
                        || request.TargetSpotGeneration != targetSpotGeneration
                        || targetAuthority
                           is not ZLinkAuthorityReadResult.Found currentTarget
                        || currentTarget.Snapshot.ObjectGeneration
                           != request.TargetSpotGeneration
                        || currentTarget.Snapshot.AuthorityOwnerGeneration
                           != request.TargetSpotAuthorityOwnerGeneration
                        || targetNodeGeneration == 0)
                        throw new ZLinkFrameworkException(
                            ZLinkFrameworkErrorKind.ActorGenerationStale,
                            $"Actor '{request.ActorId}' relocation admission target changed.");
                    ZLinkActorRelocationRegistry.TryResolve(
                        Registration,
                        request.ActorType,
                        target.NodeRid,
                        out var relocation);
                    if (relocation is null)
                        throw new ZLinkFrameworkException(
                            ZLinkFrameworkErrorKind.RequestRejected,
                            $"Actor type '{request.ActorType}' relocation policy is not registered on the target node.");
                    if (!_relocationPermits.TryAcquire(
                            ZLinkRelocationPermitRequest.Inbound(
                                request.PredictedPayloadBytes,
                                restore: relocation.PolicyKind == 2),
                            out var reservationLease))
                        throw new ZLinkFrameworkException(
                            ZLinkFrameworkErrorKind.ActorMoving,
                            $"Actor '{request.ActorId}' target relocation admission is busy.");
                    var reservation = new ZLinkActorRelocationReservation(
                        Guid.NewGuid().ToString("N"),
                        request.PredictedPayloadBytes,
                        target.NodeRid,
                        targetNodeGeneration,
                        targetSpotGeneration,
                        expectedTargetAuthorityOwnerGeneration,
                        request.TargetSpotAuthorityOwnerGeneration);
                    var payload = ZLinkRemoteActorJoinPackets.DecodeAdmissionRequestPayload(
                        request,
                        Registration.Codecs);
                    try
                    {
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
                        if (!result.Accepted)
                        {
                            reservationLease.Dispose();
                            return new ZLinkActorHandoffAdmissionDecision(
                                ZLinkRemoteActorJoinPackets.CreateAdmissionReply(
                                    false,
                                    result.Reply,
                                    Registration.Codecs,
                                    request.DeadlineUnixTimeMilliseconds),
                                default);
                        }
                        return new ZLinkActorHandoffAdmissionDecision(
                            ZLinkRemoteActorJoinPackets.CreateAdmissionReply(
                                true,
                                result.Reply,
                                Registration.Codecs,
                                request.DeadlineUnixTimeMilliseconds,
                                reservation),
                            reservationLease);
                    }
                    catch
                    {
                        reservationLease.Dispose();
                        throw;
                    }
                },
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal void AbortRoutedActorJoinAdmission(
        string spotId,
        ZLinkRemoteActorAdmissionAbortRequest request)
    {
        _actorHandoffAdmissions.AbortReservation(request, spotId);
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

    private async ValueTask ValidateActorRelocationTargetAsync(
        ZLinkRemoteActorJoinRequest request,
        ActorHandoffTarget target,
        IZLinkAuthorityStore authorityStore,
        CancellationToken cancellationToken)
    {
        var nodeGeneration = GetSpotNodeRuntime(target.NodeRid)
            .Node
            .MeshStatus()
            .LifecycleGeneration;
        var spotGeneration = target.UserSpot?.ObjectGeneration
                             ?? target.EntrySpot?.ObjectGeneration
                             ?? 0;
        var targetAuthority = await authorityStore.ReadAuthorityAsync(
                ZLinkUserSpotAuthorityPayloadCodec.AuthorityKey(
                    target.TargetRid),
                cancellationToken)
            .ConfigureAwait(false);
        if (string.IsNullOrEmpty(request.ReservationToken)
            || request.ReservedPayloadBytes <= 0
            || request.TargetNodeRid is null
            || !request.TargetNodeRid.AsSpan().SequenceEqual(
                target.NodeRid.ToBytes())
            || request.TargetNodeGeneration != nodeGeneration
            || request.TargetSpotGeneration != spotGeneration
            || targetAuthority
               is not ZLinkAuthorityReadResult.Found currentTarget
            || currentTarget.Snapshot.ObjectGeneration
               != request.TargetSpotGeneration
            || currentTarget.Snapshot.AuthorityOwnerGeneration
               != request.TargetSpotAuthorityOwnerGeneration
            || request.TargetAuthorityOwnerGeneration
               != checked(request.ActorAuthorityOwnerGeneration + 1))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorGenerationStale,
                $"Actor '{request.ActorId}' relocation target fence changed.");
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

    internal async ValueTask<ZLinkRemoteSessionBindResponse> BindRemoteBoundSessionRouteAsync(
        ZLinkRemoteSessionBindRequest request,
        RoutingId sourceNodeRid,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var targetNodeRid = RoutingId.From(request.TargetNodeRid);
        var sessionNodeRid = RoutingId.From(request.SessionNodeRid);
        var sessionRid = RoutingId.From(request.SessionRid);
        if (sourceNodeRid != sessionNodeRid)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorSessionNotBound,
                "Remote actor session binding source did not match the declared session node.");

        var actorRef = new ZLinkBackendActorRef(
            targetNodeRid,
            request.ActorId,
            request.ObjectGeneration);
        var nodeRuntime = GetSpotNodeRuntime(targetNodeRid);
        var node = nodeRuntime.Node;
        var currentNodeGeneration = node.MeshStatus().LifecycleGeneration;
        var localMeshName = ResolveSpotNodeMeshName(nodeRuntime);
        if (!string.Equals(localMeshName, request.MeshName, StringComparison.Ordinal)
            || currentNodeGeneration == 0
            || node.RoutingId != targetNodeRid
            || node is not IZLinkBackendLocalActorAuthorityReader authorityReader
            || !authorityReader.TryGetLocalActorAuthority(
                actorRef,
                out var authorityOwnerGeneration,
                out var ownerLeaseGeneration)
            || authorityOwnerGeneration == 0
            || ownerLeaseGeneration == 0)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorLocationStale,
                $"Actor '{request.ActorId}' remote session binding target lifecycle is stale.");

        BindActorSession(
            request.ActorId,
            sessionNodeRid,
            sessionRid,
            request.BindingToken,
            request.BindingGeneration,
            request.ObjectGeneration,
            authorityOwnerGeneration,
            localMeshName,
            currentNodeGeneration,
            ownerLeaseGeneration,
            request.SessionOwnerNodeGeneration,
            request.AcceptedHighWater);
        await ZLinkSessionBindingReplacement.CompletePreviousAsync(
                request.ActorId,
                targetNodeRid,
                request.PreviousBinding,
                SendPreviousTombstoneAsync,
                cancellationToken)
            .ConfigureAwait(false);
        return new ZLinkRemoteSessionBindResponse(
            true,
            request.ObjectGeneration,
            localMeshName,
            targetNodeRid.ToBytes().ToArray(),
            currentNodeGeneration,
            authorityOwnerGeneration,
            ownerLeaseGeneration);

        async ValueTask<ZLinkRemoteSessionUnbindResponse>
            SendPreviousTombstoneAsync(
                ZLinkRemoteSessionUnbindRequest unbind,
                CancellationToken token)
        {
            return await Services.GetRequiredService<IZLinkRouteClient>()
                .RequestToNode(
                    unbind.MeshName,
                    RoutingId.From(unbind.TargetNodeRid),
                    unbind)
                .Timeout(Registration.DefaultRequestTimeout)
                .Async<ZLinkRemoteSessionUnbindResponse>(token)
                .ConfigureAwait(false);
        }
    }

    internal ValueTask<ZLinkRemoteSessionUnbindResponse> UnbindRemoteBoundSessionRouteAsync(
        ZLinkRemoteSessionUnbindRequest request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var targetNodeRid = RoutingId.From(request.TargetNodeRid);
        if (!TryGetActorBoundSession(request.ActorId, out var current))
            return ValueTask.FromResult(new ZLinkRemoteSessionUnbindResponse(true));
        if (!TryGetCreatedActorState(request.ActorId, out var actorState)
            || actorState.NativeActorRef is not { } actorRef
            || actorRef.NodeRid != targetNodeRid
            || actorRef.Generation != request.ObjectGeneration
            || !string.Equals(current.BindingToken, request.BindingToken, StringComparison.Ordinal)
            || current.BindingGeneration != request.BindingGeneration
            || current.ObjectGeneration != request.ObjectGeneration
            || !string.Equals(current.MeshName, request.MeshName, StringComparison.Ordinal)
            || current.TargetNodeGeneration != request.TargetNodeGeneration
            || current.AuthorityOwnerGeneration != request.AuthorityOwnerGeneration
            || current.OwnerLeaseGeneration != request.OwnerLeaseGeneration
            || current.SessionOwnerNodeGeneration != request.SessionOwnerNodeGeneration)
            return ValueTask.FromResult(new ZLinkRemoteSessionUnbindResponse(true));

        UnbindActorSession(request.ActorId, request.BindingToken);
        return ValueTask.FromResult(new ZLinkRemoteSessionUnbindResponse(true));
    }

    private static string ResolveSpotNodeMeshName(ZLinkSpotNodeRuntime node) =>
        node.Registration.SpotMeshChannelName ?? node.Registration.SpotNodeName;

    internal ValueTask<ZLinkSpotActivation?> JoinActorToSpotAsync(
        ZLinkSpotActivation activation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        return _actorSessionManager.JoinActorToSpotAsync(activation, actor, cancellationToken);
    }

    internal ValueTask RestoreActorSpotAfterFailedCommitAsync(
        ZLinkSpotActivation failedTarget,
        ZLinkSpotActivation? previousActivation,
        IZLinkActor actor,
        CancellationToken cancellationToken = default)
    {
        return _actorSessionManager.RestoreActorSpotAfterFailedCommitAsync(
            failedTarget,
            previousActivation,
            actor,
            cancellationToken);
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
            var state = GetOrCreateActorState(result.Actor.Context.ActorId);
            var nativeRef = state.NativeActorRef
                            ?? throw new ZLinkFrameworkException(
                                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                                $"Actor '{result.Actor.Context.ActorId}' does not have a native Actor ref after creation.");
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
        ulong authorityOwnerGeneration,
        CancellationToken cancellationToken)
    {
        var result = await _actorSessionManager.PrepareReservedActorAsync(
                actorId,
                actorType,
                createRequest,
                objectGeneration,
                authorityOwnerGeneration,
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

    internal async ValueTask NotifyActorDisconnectedAsync(
        ZLinkSessionBindingEntry binding,
        CancellationToken cancellationToken = default)
    {
        var actor = binding.Route.Ref;
        var state = GetOrCreateActorState(actor.ActorId);
        if (state.Actor is not null
            && state.NativeActorRef is { } localActor
            && localActor.NodeRid == actor.NodeRid
            && localActor.Generation == actor.ObjectGeneration)
        {
            if (!state.TryGetBoundSession(out var current)
                || !string.Equals(
                    current.BindingToken,
                    binding.BindingToken,
                    StringComparison.Ordinal))
                return;
            try
            {
                await NotifyActorDisconnectedByIdAsync(actor.ActorId, cancellationToken)
                    .ConfigureAwait(false);
            }
            finally
            {
                RemoveActorSessionBinding(actor.ActorId, binding.BindingToken);
            }
            return;
        }

        var node = GetMeshNodeRuntime(binding.MeshName).Node;

        await _actorBoundSessionCoordinator.NotifyRemoteDisconnectedAsync(
                binding,
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
        ZLinkSessionActor actorRef,
        ulong bindingGeneration,
        ZLinkSessionBindingRoute route,
        ulong sessionOwnerNodeGeneration)
    {
        return _actorBoundSessionCoordinator.BindSessionActor(
            actorId,
            context,
            bindingToken,
            actorRef,
            bindingGeneration,
            route,
            sessionOwnerNodeGeneration);
    }

    internal ulong NextSessionBindingGeneration()
        => _actorBoundSessionCoordinator.NextBindingGeneration();

    internal bool TryAcceptSessionActorFrame(
        string actorId,
        string bindingToken,
        out ulong acceptedHighWater)
        => _actorBoundSessionCoordinator.TryAcceptSessionFrame(
            actorId,
            bindingToken,
            out acceptedHighWater);

    internal ZLinkSessionRouteCommitResult CommitSessionActorRoute(
        ZLinkSessionRouteCommit request)
        => _actorBoundSessionCoordinator.CommitSessionRoute(request);

    internal ValueTask<ZLinkSessionRouteSealResult> SealSessionActorRouteAsync(
        ZLinkSessionRouteSeal request,
        CancellationToken cancellationToken)
        => _actorBoundSessionCoordinator.SealSessionRouteAsync(
            request,
            cancellationToken);

    internal void CompleteAcceptedSessionActorFrame(
        string actorId,
        string bindingToken)
        => _actorBoundSessionCoordinator.CompleteAcceptedSessionFrame(
            actorId,
            bindingToken);

    internal string TrackRemoteSessionActorRequest(
        string actorId,
        ulong requestId,
        string bindingToken)
        => _actorBoundSessionCoordinator.TrackRemoteSessionRequest(
            actorId,
            requestId,
            bindingToken);

    internal void CompleteRemoteSessionActorRequest(
        string actorId,
        ulong requestId)
        => _actorBoundSessionCoordinator.CompleteRemoteSessionRequest(
            actorId,
            requestId);

    internal bool AbortSessionActorRouteSeal(
        ZLinkSessionRouteSeal request)
        => _actorBoundSessionCoordinator.AbortSessionRouteSeal(request);

    internal bool UnsealCommittedSessionActorRoute(
        ZLinkSessionRouteCommit request)
        => _actorBoundSessionCoordinator.UnsealCommittedSessionRoute(request);

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
        RoutingId targetNodeRid,
        ulong targetNodeGeneration,
        ulong authorityOwnerGeneration,
        ulong ownerLeaseGeneration,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        MeshOperationId operationId,
        byte forwardingHopCount,
        ulong replyRequestId,
        uint replyFlags,
        string? replyCapability,
        byte[] header,
        byte[] body,
        CancellationToken cancellationToken)
    {
        // The relay target is this node. Preserve the generation carried by
        // the incoming stale route instead of reading NativeActorRef: during
        // a chained transfer that state already points at the next owner, and
        // replacing the incoming identity would bypass this node's forwarding
        // mapping and attempt blocked local dispatch.
        var targetNode = GetSpotNodeRuntime(targetNodeRid).Node;
        if (targetNode.MeshStatus().LifecycleGeneration != targetNodeGeneration
            || authorityOwnerGeneration == 0
            || ownerLeaseGeneration == 0)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorLocationStale,
                $"Actor '{actorId}' session relay target lifecycle is stale.");
        var state = GetOrCreateActorState(actorId);
        var actorRef = new ZLinkBackendActorRef(
            targetNodeRid,
            actorId,
            actorGeneration);
        var routeContext = new ZLinkBackendActorRouteContext(
            operationId,
            forwardingHopCount,
            targetNodeGeneration,
            authorityOwnerGeneration,
            ownerLeaseGeneration,
            replyRequestId,
            replyFlags,
            replyCapability);
        if (routeContext.IsDirectRoute)
        {
            if (forwardingHopCount is 0 or > 8
                || sourceNodeRid.IsEmpty
                || targetNode is not IZLinkBackendLocalActorAuthorityReader authorityReader
                || !authorityReader.TryGetLocalActorAuthority(
                    actorRef,
                    out var currentAuthorityOwnerGeneration,
                    out var currentOwnerLeaseGeneration)
                || currentAuthorityOwnerGeneration != authorityOwnerGeneration
                || currentOwnerLeaseGeneration != ownerLeaseGeneration)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorLocationStale,
                    $"Actor '{actorId}' direct relay authority identity is stale.");
        }
        else if (forwardingHopCount != 0
                 || ((replyRequestId != 0 || replyFlags != 0)
                     && !ZLinkActorBoundSessionRelay.IsNoBindRequest(
                         replyRequestId,
                         replyFlags))
                 || !state.TryGetBoundSession(out var session)
                 || session.ObjectGeneration != actorGeneration
                 || !string.Equals(
                     session.MeshName,
                     ResolveSpotNodeMeshName(GetSpotNodeRuntime(targetNodeRid)),
                     StringComparison.Ordinal)
                 || session.TargetNodeGeneration != targetNodeGeneration
                 || session.AuthorityOwnerGeneration != authorityOwnerGeneration
                 || session.OwnerLeaseGeneration != ownerLeaseGeneration)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorLocationStale,
                $"Actor '{actorId}' session relay authority identity is stale.");
        }
        var parts = new[]
        {
            new ZLinkBackendActorPart(
                actorRef, sourceNodeRid, sourceSessionRid, replyRequestId, replyFlags,
                Message.From(header), More: true, RouteContext: routeContext),
            new ZLinkBackendActorPart(
                actorRef, sourceNodeRid, sourceSessionRid, replyRequestId, replyFlags,
                Message.From(body), More: false, RouteContext: routeContext)
        };
        var batch = ZLinkActorHandoffIngress.CaptureMovingFrames(this, parts);
        if (batch.Count == 0) return;
        if (!routeContext.IsDirectRoute)
            state.RecordRelocatedSessionAccepted(sourceSessionRid);
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
        ulong targetNodeGeneration,
        ulong authorityOwnerGeneration,
        ulong ownerLeaseGeneration,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ZLinkBackendActorRouteContext routeContext,
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
            actor.NodeRid.ToHex(),
            targetNodeGeneration,
            authorityOwnerGeneration,
            ownerLeaseGeneration,
            sessionNodeRid.ToHex(),
            sourceSessionRid.ToHex(),
            routeContext.OperationId.High,
            routeContext.OperationId.Low,
            routeContext.ForwardingHopCount,
            routeContext.ReplyRequestId,
            routeContext.ReplyFlags,
            routeContext.ReplyCapability,
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
        ZLinkRemoteSessionPushRelay identity,
        byte[] frame,
        CancellationToken cancellationToken)
    {
        var deadline = DateTime.UtcNow + Registration.DefaultRequestTimeout;
        while (true)
        {
            var delivery = _actorBoundSessionCoordinator.DeliverLocalSessionFrame(
                identity,
                frame);
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
        ZLinkActorBoundSession session,
        byte[] frame)
    {
        var nodeRuntime = GetMeshNodeRuntime(session.MeshName);
        var actorRef = GetOrCreateActorState(actorId).NativeActorRef
                       ?? throw new ZLinkFrameworkException(
                           ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorId}' session route has no local Actor ref.");
        if (actorRef.Generation != session.ObjectGeneration)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorGenerationStale,
                $"Actor '{actorId}' session route ObjectGeneration is stale.");
        var sessionNodeRid = session.SessionNodeRid
                             ?? throw new ZLinkFrameworkException(
                                 ZLinkFrameworkErrorKind.ActorSessionNotBound,
                                 $"Actor '{actorId}' session route has no owner node.");
        var relayMessage = new ZLinkRemoteSessionPushRelay(
            actorId,
            session.ObjectGeneration,
            session.MeshName,
            actorRef.NodeRid.ToHex(),
            session.TargetNodeGeneration,
            session.AuthorityOwnerGeneration,
            session.OwnerLeaseGeneration,
            session.BindingToken,
            session.BindingGeneration,
            session.SessionOwnerNodeGeneration,
            session.SessionRid.ToHex(),
            frame);
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            session.MeshName,
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

    internal bool TryGetSessionActorBinding(
        string actorId,
        string bindingToken,
        out ZLinkSessionBindingEntry entry) =>
        _actorBoundSessionCoordinator.TryGetSessionBinding(
            actorId,
            bindingToken,
            out entry);

    internal bool TryGetSessionActorRoute(
        string actorId,
        string bindingToken,
        ZLinkSessionActor actorRef,
        out ZLinkSessionBindingRoute route) =>
        _actorBoundSessionCoordinator.TryGetSessionRoute(
            actorId,
            bindingToken,
            actorRef,
            out route);

    internal bool TryGetSessionActorBinding(
        string actorId,
        out ZLinkSessionBindingEntry entry) =>
        _actorBoundSessionCoordinator.TryGetSessionBindingByActorId(
            actorId,
            out entry);

    internal IReadOnlyCollection<IZLinkSessionActor> SnapshotSessionActors(
        ZLinkSessionContext context) =>
        _actorBoundSessionCoordinator.SnapshotSessionActors(context);

    internal ZLinkSessionActor? FindSessionActor(
        ZLinkSessionContext context,
        string actorId) =>
        _actorBoundSessionCoordinator.FindSessionActor(context, actorId);

    internal void BindActorSession(
        string actorId,
        RoutingId? sessionNodeRid,
        RoutingId sessionRid,
        string bindingToken,
        ulong bindingGeneration = 1,
        ulong objectGeneration = 0,
        ulong authorityOwnerGeneration = 0,
        string meshName = "",
        ulong targetNodeGeneration = 1,
        ulong ownerLeaseGeneration = 0,
        ulong sessionOwnerNodeGeneration = 1,
        ulong acceptedHighWater = 0)
    {
        _actorBoundSessionCoordinator.BindActorSession(
            actorId,
            sessionNodeRid,
            sessionRid,
            bindingToken,
            bindingGeneration,
            objectGeneration,
            authorityOwnerGeneration,
            meshName,
            targetNodeGeneration,
            ownerLeaseGeneration,
            sessionOwnerNodeGeneration,
            acceptedHighWater);
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

    internal ZLinkAsyncSubmitter CreateActorBoundSessionSubmitter(
        string meshName)
    {
        return _actorBoundSessionCoordinator.CreateSubmitter(meshName);
    }

    internal void ReplyActorNoBind(
        ZLinkBackendActorRef actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        string? replyCapability,
        IReadOnlyList<Message> parts)
    {
        var nodeRuntime = actor.NodeRid.IsEmpty
            ? GetActorClientSpotNodeRuntime()
            : GetSpotNodeRuntime(actor.NodeRid);

        if (!sourceNodeRid.IsEmpty
            && !sourceNodeRid.Equals(nodeRuntime.Node.RoutingId))
        {
            if (string.IsNullOrWhiteSpace(replyCapability))
            {
                ZLinkMessageParts.DisposeAll(parts);
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RequestProtocolError,
                    "Remote Actor reply did not preserve its reply capability.");
            }
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
                replyCapability,
                nodeRuntime.Node.RoutingId.ToHex(),
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
            ZLinkMessageParts.DisposeAll(parts);
            if (submit != SubmitResult.Ok)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RouteNotConnected,
                    $"Actor reply relay to node '{sourceNodeRid}' was not admitted.",
                    true);
            return;
        }

        if (_actorBoundSessionCoordinator.ReplyNoBind(
                actor, sourceNodeRid, sourceSessionRid, requestId, flags, parts))
            return;

        ZLinkMessageParts.DisposeAll(parts);
    }

    internal async ValueTask DeliverRemoteActorReplyAsync(
        string actorId,
        ulong requestId,
        uint flags,
        string replyCapability,
        RoutingId sourceNodeRid,
        RoutingId responderNodeRid,
        byte[] frame,
        CancellationToken cancellationToken)
    {
        if (!_actorBoundSessionCoordinator.TryClaimRemoteSessionReply(
                actorId,
                requestId,
                flags,
                replyCapability,
                sourceNodeRid,
                responderNodeRid,
                out var claim))
            return;

        var deadline = DateTime.UtcNow + Registration.DefaultRequestTimeout;
        using (claim)
        {
            while (true)
            {
                var delivery = claim.Deliver(frame);
                var retryable = delivery
                    is ZLinkActorBoundSessionCoordinator.RemotePushDelivery.Backpressured
                    or ZLinkActorBoundSessionCoordinator.RemotePushDelivery.NoBinding;
                if (!retryable || DateTime.UtcNow >= deadline)
                    return;
                await Task.Delay(TimeSpan.FromMilliseconds(10), cancellationToken)
                    .ConfigureAwait(false);
            }
        }
    }

    internal bool ForwardActorBoundSessionPart(
        string meshName,
        ZLinkBackendActorRef actorRef,
        ulong targetNodeGeneration,
        ulong authorityOwnerGeneration,
        ulong ownerLeaseGeneration,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        Message message,
        bool hasMore,
        SendFlags flags,
        ZLinkBackendActorRouteContext routeContext = default)
    {
        return _actorBoundSessionCoordinator.ForwardPart(
            actorRef,
            sourceNodeRid,
            sourceSessionRid,
            message,
            hasMore,
            flags,
            meshName,
            GetMeshNodeRuntime(meshName).Node,
            targetNodeGeneration,
            authorityOwnerGeneration,
            ownerLeaseGeneration,
            routeContext);
    }

    internal ValueTask CloseActorBoundSessionAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        return _actorBoundSessionCoordinator.CloseAsync(actorId, cancellationToken);
    }
}
