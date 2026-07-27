using Systems.Zlink.Framework.Runtime.Protocol;

namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    internal bool TryAcquireInboundSpotRelocation(
        long payloadBytes,
        bool restore,
        bool allowOversizedPayload,
        out IDisposable permit)
    {
        if (_drainAdmission.IsDraining)
        {
            permit = null!;
            return false;
        }
        if (_relocationPermits.TryAcquire(
                ZLinkRelocationPermitRequest.Inbound(
                    payloadBytes,
                    restore,
                    allowOversizedPayload),
                out var acquired))
        {
            permit = acquired;
            return true;
        }
        permit = null!;
        return false;
    }

    internal async ValueTask<ZLinkRelocationReplyAckState> RelayRelocationReplyAsync(
        RoutingId sourceNodeRid,
        ZLinkServiceWireCodec.ReplyRelayRecord relay,
        ZLinkServiceWireCodec.RequestSourceFence expectedSource,
        IReadOnlyList<byte[]> payload,
        CancellationToken cancellationToken)
    {
        var node = GetSpotNodeRuntime(relay.Coordinator.NodeRid);
        var messages = payload.Select(static part => Message.From(part)).ToArray();
        ZLinkServiceWireCodec.ReplyRelayAckRecord ack;
        try
        {
            ack = await node.RelayRelocationReplyAsync(
                    sourceNodeRid,
                    relay,
                    expectedSource,
                    messages,
                    node.Registration.DefaultRequestTimeout
                    ?? Registration.DefaultRequestTimeout,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(messages);
        }
        if (ack.RequestSource != expectedSource)
            return ZLinkRelocationReplyAckState.NotAcknowledged;
        return ack.Status == 2
            ? ZLinkRelocationReplyAckState.AlreadyTerminal
            : ZLinkRelocationReplyAckState.TerminalReceived;
    }

    internal async ValueTask<TargetStage> StageInboundSpotAggregateAsync(
        ZLinkCanonicalSpotStageContext request,
        ZLinkRelocationEnvelope envelope,
        IDisposable inboundPermit,
        ZLinkPreparedAggregateRelocation? preparedAggregate,
        CancellationToken cancellationToken)
    {
        var targetRid = RoutingId.FromHex(request.TargetNodeRid);
        var node = GetSpotNodeRuntime(targetRid);
        var spotParticipant = envelope.Participants.Single(
            static participant => participant.ObjectKind
                is ZLinkPlacementObjectKind.UserSpot
                or ZLinkPlacementObjectKind.InstanceSpot);
        if (spotParticipant.ObjectGeneration == 0
            || request.SpotId.Length == 0)
            throw new InvalidDataException(
                "The inbound SPOT relocation target does not match its envelope.");

        var targetOwnerGeneration = checked(
            spotParticipant.AuthorityOwnerGeneration + 1);
        PreparedReservedSpot preparedSpot;
        if (request.InstanceSpot)
        {
            preparedSpot = await node.Catalog.PrepareInstanceReservedAsync(
                    request.StableType,
                    request.SpotId,
                    spotParticipant.ObjectGeneration,
                    targetOwnerGeneration,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        else
        {
            if (!node.Registration.SpotRelocations.TryGetValue(
                    request.StableType,
                    out var spotRegistration))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotTypeMismatch,
                    $"User SPOT type '{request.StableType}' is not registered on target node.");
            preparedSpot = await node.Catalog.PrepareReservedAsync(
                    spotRegistration.InstanceType,
                    request.SpotId,
                    spotParticipant.ObjectGeneration,
                    targetOwnerGeneration,
                    ZLinkMessage.Empty,
                    cancellationToken)
                .ConfigureAwait(false);
        }

        var actorStates = new List<ZLinkActorRuntimeState>();
        ZLinkSpotRelocationSeal? targetAdmissionSeal = null;
        try
        {
            await preparedSpot.Activation.RestoreSpotRelocationStateAsync(
                    spotParticipant.ApplicationState,
                    cancellationToken)
                .ConfigureAwait(false);

            var descriptors = request.Actors.ToDictionary(
                static actor => actor.ActorId,
                StringComparer.Ordinal);
            foreach (var participant in envelope.Participants.Where(
                         static participant =>
                             participant.ObjectKind
                             == ZLinkPlacementObjectKind.Actor))
            {
                var descriptor = descriptors.Values.Single(candidate =>
                    ZLinkActorAuthorityPayloadCodec.AuthorityKey(
                        candidate.ActorId)
                    == participant.AuthorityKey);
                if (!ZLinkActorAuthorityPayloadCodec.TryDecodeRelocating(
                        descriptor.AuthorityPayload,
                        out var authority)
                    || !ZLinkActorRelocationAuthorityPayloadCodec.TryDecode(
                        descriptor.AuthorityPayload,
                        out var relocationAuthority)
                    || relocationAuthority.RelocationId
                       != envelope.AggregateId
                    || relocationAuthority.Phase
                       != ZLinkActorRelocationAuthorityPhase.Activated
                    || !node.Registration.ActorRelocations.TryGetValue(
                        authority.StableType,
                        out var relocation))
                    throw new InvalidDataException(
                        $"Actor participant '{participant.AuthorityKey.Value}' cannot be restored on the target.");

                var creation = await _actorSessionManager
                    .RelocateAndBindActorAsync(
                        authority.ActorId,
                        authority.StableType,
                        relocation,
                        participant.ApplicationState,
                        participant.ObjectGeneration,
                        checked(participant.AuthorityOwnerGeneration + 1),
                        ZLinkActorClaimMode.StagedRelocation,
                        publishActorRef: false,
                        cancellationToken)
                    .ConfigureAwait(false);
                var actorState = GetOrCreateActorState(authority.ActorId);
                actorState.StageRelocationSessionRoute(
                    envelope.AggregateId.ToString("N"),
                    relocationAuthority.BoundSessionRoute);
                await preparedSpot.Activation
                    .PrepareTransferredActorJoinAndReplayAsync(
                        creation.Actor,
                        actorState,
                        cancellationToken)
                    .ConfigureAwait(false);
                actorStates.Add(actorState);
            }

            if (!preparedSpot.Activation.TrySealRelocation(
                    out targetAdmissionSeal))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotMoving,
                    $"Target SPOT '{request.SpotId}' could not seal staging admission.",
                    true);
            preparedSpot.Activation.RestoreLogicalTimers(
                spotParticipant.LogicalTimers);
            return new TargetStage(
                node,
                preparedSpot,
                envelope,
                actorStates,
                request.StableType,
                request.MeshName,
                RoutingId.FromHex(request.SourceNodeRid),
                request.SourceNodeLifecycleGeneration,
                new ZLinkLocationOwnerToken(
                    request.SourceOwnerId,
                    request.SourceOwnerLeaseGeneration),
                request.RelocationReference,
                request.RelocationChecksum,
                DateTimeOffset.UtcNow
                + ZLinkSpotRetireTargetRuntime.StageRetention,
                ZLinkSpotRetireTargetRuntime.ComputeStageRequestDigest(
                    request),
                targetAdmissionSeal
                ?? throw new InvalidOperationException(
                    "Target staging admission seal was not created."),
                inboundPermit,
                preparedAggregate,
                spotParticipant.AuthorityOwnerGeneration,
                targetOwnerGeneration,
                request.MeshName,
                request.TargetNodeLifecycleGeneration,
                request.TargetOwnerLeaseGeneration,
                request.TargetAttemptGeneration);
        }
        catch
        {
            if (targetAdmissionSeal is not null)
                _ = preparedSpot.Activation.AbortRelocation(
                    targetAdmissionSeal);
            foreach (var actorState in actorStates)
            {
                try
                {
                    await _actorSessionManager.RollbackTransferredActorAsync(
                            actorState.ActorId,
                            CancellationToken.None)
                        .ConfigureAwait(false);
                }
                catch
                {
                }
            }
            await node.Catalog.DiscardReservedAsync(preparedSpot)
                .ConfigureAwait(false);
            throw;
        }
    }

    internal async ValueTask PublishInboundSpotAggregateAsync(
        TargetStage stage,
        Func<CancellationToken, ValueTask> normalizeAuthority,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(normalizeAuthority);
        await stage.PublishGate.WaitAsync(cancellationToken)
            .ConfigureAwait(false);
        var spotParticipant = stage.Envelope.Participants.Single(
            static participant => participant.ObjectKind
                is ZLinkPlacementObjectKind.UserSpot
                or ZLinkPlacementObjectKind.InstanceSpot);
        try
        {
            if (Volatile.Read(ref stage.Published) != 0)
                return;

            await PrepareInboundSpotAggregateCoreAsync(
                    stage,
                    spotParticipant,
                    cancellationToken)
                .ConfigureAwait(false);
            var handoffId = stage.Envelope.AggregateId.ToString("N");
            var sessionCommits =
                new List<(ZLinkActorRuntimeState State,
                    ZLinkSessionRouteCommitRequest Request)>();
            foreach (var actorState in stage.ActorStates)
            {
                var actorRef = actorState.NativeActorRef
                               ?? throw new ZLinkFrameworkException(
                                   ZLinkFrameworkErrorKind.ActorRouteNotFound,
                                   $"Actor '{actorState.ActorId}' has no staged target reference.");
                actorState.MarkRelocationSessionAuthorityCommitted(
                    handoffId,
                    actorRef,
                    stage.TargetAuthorityOwnerGeneration,
                    stage.TargetMeshName,
                    stage.TargetNodeLifecycleGeneration,
                    stage.TargetOwnerLeaseGeneration);
                var sessionCommit = await CommitCompletedSessionRouteAsync(
                        actorState,
                        handoffId,
                        cancellationToken)
                    .ConfigureAwait(false);
                if (sessionCommit is not null)
                    sessionCommits.Add((actorState, sessionCommit));
            }

            await PublishCatalogBeforeNormalizationAsync(
                    stage,
                    () =>
                    {
                        foreach (var actorState in stage.ActorStates)
                            _actorSessionManager.PublishReservedActor(
                                actorState.ActorId);
                        stage.Node.Catalog.PublishRelocatedReserved(
                            stage.Spot);
                    },
                    () => normalizeAuthority(cancellationToken))
                .ConfigureAwait(false);
            foreach (var (actorState, sessionCommit) in sessionCommits)
            {
                await UnsealCompletedSessionRouteAsync(
                        sessionCommit,
                        cancellationToken)
                    .ConfigureAwait(false);
                actorState.CompleteRelocationSessionRoute(handoffId);
            }

            if (!stage.Spot.Activation.AbortRelocation(
                    stage.TargetAdmissionSeal))
                throw new InvalidOperationException(
                    $"Target SPOT '{stage.Spot.Activation.SpotId}' lost its staging admission seal.");
            Volatile.Write(ref stage.Published, 1);
        }
        finally
        {
            stage.PublishGate.Release();
        }
    }

    internal static async ValueTask PublishCatalogBeforeNormalizationAsync(
        TargetStage stage,
        Action publishCatalog,
        Func<ValueTask> normalizeAuthority)
    {
        ArgumentNullException.ThrowIfNull(stage);
        ArgumentNullException.ThrowIfNull(publishCatalog);
        ArgumentNullException.ThrowIfNull(normalizeAuthority);
        if (Volatile.Read(ref stage.LocalCatalogPublished) == 0)
        {
            publishCatalog();
            Volatile.Write(ref stage.LocalCatalogPublished, 1);
        }
        await normalizeAuthority().ConfigureAwait(false);
    }

    internal async ValueTask PrepareInboundSpotAggregateAsync(
        TargetStage stage,
        CancellationToken cancellationToken)
    {
        await stage.PublishGate.WaitAsync(cancellationToken)
            .ConfigureAwait(false);
        try
        {
            if (Volatile.Read(ref stage.Published) != 0)
                return;
            var spotParticipant = stage.Envelope.Participants.Single(
                static participant => participant.ObjectKind
                    is ZLinkPlacementObjectKind.UserSpot
                    or ZLinkPlacementObjectKind.InstanceSpot);
            await PrepareInboundSpotAggregateCoreAsync(
                    stage,
                    spotParticipant,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            stage.PublishGate.Release();
        }
    }

    private async ValueTask PrepareInboundSpotAggregateCoreAsync(
        TargetStage stage,
        ZLinkRelocationParticipantEnvelope spotParticipant,
        CancellationToken cancellationToken)
    {
        foreach (var actorState in stage.ActorStates)
            await stage.Spot.Activation
                .CompleteTransferredActorJoinSealedAsync(
                    actorState,
                    stage.TargetAdmissionSeal,
                    cancellationToken)
                .ConfigureAwait(false);
        await stage.Node.Catalog.ValidateRelocatedReservedAsync(
                stage.Spot,
                stage.StableType,
                spotParticipant.ObjectGeneration,
                stage.TargetAuthorityOwnerGeneration,
                cancellationToken)
            .ConfigureAwait(false);
        var replayJobs = spotParticipant.AcceptedJobs
            .Concat(stage.HeldRecords)
            .OrderBy(static job => job.AcceptedSequence)
            .ToArray();
        if (!stage.Envelope.CanonicalLogicalStream.IsEmpty)
            await RestoreCanonicalReplayProgressAsync(
                    stage,
                    replayJobs,
                    cancellationToken)
                .ConfigureAwait(false);
        var replayed = Volatile.Read(ref stage.ReplayedJobCount);
        if (replayed > replayJobs.Length)
            throw new ZLinkRelocationDataLostException(
                $"Target SPOT '{stage.Spot.Activation.SpotId}' replay cursor exceeds its accepted journal.");
        if (replayed == replayJobs.Length)
            return;
        await stage.Spot.Activation.ReplayAcceptedJobsAsync(
                replayJobs,
                stage.SourceMeshName,
                stage.TargetAdmissionSeal,
                Volatile.Read(ref stage.ReplayedJobCount),
                (job, journal, capturedReply, ct) =>
                    CompleteAcceptedReplayAsync(
                        stage,
                        job,
                        journal,
                        capturedReply,
                        ct),
                cancellationToken)
            .ConfigureAwait(false);
        if (Volatile.Read(ref stage.ReplayedJobCount) != replayJobs.Length)
            throw new ZLinkRelocationDataLostException(
                $"Target SPOT '{stage.Spot.Activation.SpotId}' did not complete every accepted replay.");
    }

    private async ValueTask RestoreCanonicalReplayProgressAsync(
        TargetStage stage,
        IReadOnlyList<ZLinkRelocationQueuedJob> replayJobs,
        CancellationToken cancellationToken)
    {
        var authorityStore = Registration.Locations.ResolveStore()
                             ?? throw new ZLinkConfigurationException(
                                 "Location Store is not registered.");
        var relocationStore = Registration.Locations.RelocationStoreInstance
                              ?? throw new ZLinkConfigurationException(
                                  "Relocation Store is not registered.");
        var targetOwner = LocationLifecycle?.OwnerToken
                          ?? throw new ZLinkConfigurationException(
                              "Location runtime is not registered.");
        var targetDescriptor = new ZLinkMeshNodeDescriptorKey(
            stage.TargetMeshName,
            stage.Node.Node.RoutingId);
        var coordinator = new ZLinkAggregateRelocationCoordinator(
            authorityStore,
            relocationStore);
        var current = await coordinator.ReadCanonicalRootAsync(
                stage.Envelope,
                targetOwner,
                cancellationToken)
            .ConfigureAwait(false);
        var participant = current.Participants.Single(static candidate =>
            candidate.ObjectKind is ZLinkPlacementObjectKind.UserSpot
                or ZLinkPlacementObjectKind.InstanceSpot);

        foreach (var completion in participant.TerminalCompletions
                     .Where(static candidate => candidate.DeliveryState == 0))
        {
            var job = replayJobs.SingleOrDefault(candidate =>
                candidate.AcceptedSequence == completion.AcceptedSequence
                && candidate.CanonicalRequest is { } request
                && request.OperationHigh == completion.OperationHigh
                && request.OperationLow == completion.OperationLow)
                      ?? throw new ZLinkRelocationDataLostException(
                          "Canonical pending reply has no matching accepted request.");
            var request = job.CanonicalRequest!;
            var replyParts = EncodeCanonicalReply(
                stage.SourceMeshName,
                request,
                completion);
            var acknowledged = await TryRelayCanonicalReplyAsync(
                    RoutingId.FromHex(completion.SourceNodeRid),
                    await CreateCanonicalReplyRelayAsync(
                            authorityStore,
                            stage,
                            participant,
                            request,
                            completion,
                            cancellationToken)
                        .ConfigureAwait(false),
                    completion,
                    replyParts,
                    cancellationToken)
                .ConfigureAwait(false);
            if (!await CompleteCanonicalReplyDeliveryAsync(
                    stage,
                    completion,
                    acknowledged,
                    coordinator,
                    targetDescriptor,
                    targetOwner,
                    cancellationToken)
                .ConfigureAwait(false))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotMoving,
                    "The canonical pending reply has neither an acknowledgement nor exact source lease expiry proof.",
                    true);
        }

        Interlocked.Exchange(
            ref stage.ReplayedJobCount,
            replayJobs.Count(job => job.AcceptedSequence <= participant.ReplayCursor));
    }

    private async ValueTask CompleteAcceptedReplayAsync(
        TargetStage stage,
        ZLinkRelocationQueuedJob job,
        ZLinkSpotAcceptedJournalRecord journal,
        byte[][]? capturedReply,
        CancellationToken cancellationToken)
    {
        if (stage.Envelope.CanonicalLogicalStream.IsEmpty)
        {
            if (capturedReply is not null)
                throw new ZLinkRelocationDataLostException(
                    "A legacy relocation reply has no exact request-source fence for service-wire relay.");
            Interlocked.Increment(ref stage.ReplayedJobCount);
            return;
        }

        var request = job.CanonicalRequest
                      ?? throw new ZLinkRelocationDataLostException(
                          "Canonical replay record has no request projection.");
        ZLinkCanonicalTerminalCompletion? completion = null;
        if (request.ReplyRouteId != 0)
        {
            if (capturedReply is null)
                throw new ZLinkRelocationDataLostException(
                    "Canonical request replay completed without a terminal reply.");
            completion = ZLinkRelocationEnvelopeCodec
                .CreateCanonicalTerminalCompletion(
                    request.OperationHigh,
                    request.OperationLow,
                    request.Source.OwnerId,
                    request.Source.OwnerLeaseGeneration,
                    request.Source.NodeRid,
                    request.Source.NodeGeneration,
                    stage.Envelope.Participants.Single(static participant =>
                            participant.ObjectKind
                            is ZLinkPlacementObjectKind.UserSpot
                            or ZLinkPlacementObjectKind.InstanceSpot)
                        .CanonicalParticipantId,
                    job.AcceptedSequence,
                    terminalResult: 0,
                    errorCode: 0,
                    deliveryState: 0,
                    DecodeCanonicalReply(capturedReply));
        }

        var authorityStore = Registration.Locations.ResolveStore()
                             ?? throw new ZLinkConfigurationException(
                                 "Location Store is not registered.");
        var relocationStore = Registration.Locations.RelocationStoreInstance
                              ?? throw new ZLinkConfigurationException(
                                  "Relocation Store is not registered.");
        var targetOwner = LocationLifecycle?.OwnerToken
                          ?? throw new ZLinkConfigurationException(
                              "Location runtime is not registered.");
        var targetDescriptor = new ZLinkMeshNodeDescriptorKey(
            stage.TargetMeshName,
            stage.Node.Node.RoutingId);
        var coordinator = new ZLinkAggregateRelocationCoordinator(
            authorityStore,
            relocationStore);
        _ = await coordinator.AdvanceCanonicalReplayAsync(
                stage.Envelope,
                stage.Envelope.Participants.Single(static participant =>
                        participant.ObjectKind
                        is ZLinkPlacementObjectKind.UserSpot
                        or ZLinkPlacementObjectKind.InstanceSpot)
                    .CanonicalParticipantId,
                job.AcceptedSequence,
                completion,
                targetDescriptor,
                stage.TargetNodeLifecycleGeneration,
                targetOwner,
                cancellationToken)
            .ConfigureAwait(false);

        if (completion is not null)
        {
            var sourceNodeRid = RoutingId.FromHex(completion.SourceNodeRid);
            var participant = stage.Envelope.Participants.Single(
                static candidate => candidate.ObjectKind
                    is ZLinkPlacementObjectKind.UserSpot
                    or ZLinkPlacementObjectKind.InstanceSpot);
            var acknowledged = await TryRelayCanonicalReplyAsync(
                    sourceNodeRid,
                    await CreateCanonicalReplyRelayAsync(
                            authorityStore,
                            stage,
                            participant,
                            request,
                            completion,
                            cancellationToken)
                        .ConfigureAwait(false),
                    completion,
                    capturedReply!,
                    cancellationToken)
                .ConfigureAwait(false);
            if (!await CompleteCanonicalReplyDeliveryAsync(
                    stage,
                    completion,
                    acknowledged,
                    coordinator,
                    targetDescriptor,
                    targetOwner,
                    cancellationToken)
                .ConfigureAwait(false))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.SpotMoving,
                    "The canonical request reply has neither an acknowledgement nor exact source lease expiry proof.",
                    true);
        }

        Interlocked.Increment(ref stage.ReplayedJobCount);
    }

    private async ValueTask<bool> CompleteCanonicalReplyDeliveryAsync(
        TargetStage stage,
        ZLinkCanonicalTerminalCompletion completion,
        ZLinkRelocationReplyAckState acknowledgement,
        ZLinkAggregateRelocationCoordinator coordinator,
        ZLinkMeshNodeDescriptorKey targetDescriptor,
        ZLinkLocationOwnerToken targetOwner,
        CancellationToken cancellationToken)
    {
        if (acknowledgement is
            ZLinkRelocationReplyAckState.TerminalReceived
            or ZLinkRelocationReplyAckState.AlreadyTerminal)
        {
            _ = await coordinator.AcknowledgeCanonicalReplyAsync(
                    stage.Envelope,
                    completion,
                    acknowledgement
                    == ZLinkRelocationReplyAckState.AlreadyTerminal
                        ? (byte)2
                        : (byte)1,
                    targetDescriptor,
                    stage.TargetNodeLifecycleGeneration,
                    targetOwner,
                    cancellationToken)
                .ConfigureAwait(false);
            return true;
        }

        var store = Registration.Locations.ResolveStore()
                    ?? throw new ZLinkConfigurationException(
                        "Location Store is not registered.");
        var lease = await store.ReadOwnerLeaseAsync(
                completion.SourceOwnerId,
                cancellationToken)
            .ConfigureAwait(false);
        if (!IsExactSourceLeaseExpired(lease, completion))
            return false;
        _ = await coordinator.ExpireCanonicalReplySourceLeaseAsync(
                stage.Envelope,
                completion,
                targetDescriptor,
                stage.TargetNodeLifecycleGeneration,
                targetOwner,
                cancellationToken)
            .ConfigureAwait(false);
        return true;
    }

    private async ValueTask<ZLinkRelocationReplyAckState>
        TryRelayCanonicalReplyAsync(
        RoutingId sourceNodeRid,
        ZLinkServiceWireCodec.ReplyRelayRecord relay,
        ZLinkCanonicalTerminalCompletion completion,
        IReadOnlyList<byte[]> payload,
        CancellationToken cancellationToken)
    {
        try
        {
            return await RelayRelocationReplyAsync(
                    sourceNodeRid,
                    relay,
                    new ZLinkServiceWireCodec.RequestSourceFence(
                        completion.SourceOwnerId,
                        completion.SourceOwnerLeaseGeneration,
                        RoutingId.FromHex(completion.SourceNodeRid),
                        completion.SourceNodeGeneration),
                    payload,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            throw;
        }
        catch
        {
            return ZLinkRelocationReplyAckState.NotAcknowledged;
        }
    }

    private async ValueTask<ZLinkServiceWireCodec.ReplyRelayRecord>
        CreateCanonicalReplyRelayAsync(
            IZLinkLocationStore authorityStore,
            TargetStage stage,
            ZLinkRelocationParticipantEnvelope participant,
            ZLinkCanonicalAcceptedRequest request,
            ZLinkCanonicalTerminalCompletion completion,
            CancellationToken cancellationToken)
    {
        var authority = await authorityStore.ReadAuthorityAsync(
                participant.AuthorityKey,
                cancellationToken)
            .ConfigureAwait(false);
        if (authority is not ZLinkAuthorityReadResult.Found found)
            throw new ZLinkRelocationDataLostException(
                "The canonical reply relay authority is missing.");
        var targetOwner = LocationLifecycle?.OwnerToken
                          ?? throw new ZLinkConfigurationException(
                              "Location runtime is not registered.");
        if (!ZLinkCanonicalRelocationAuthorityStateCodec.TryRead(
                found.Snapshot.Payload.Span,
                out var canonical)
            || !IsExactCanonicalReplyRelayTarget(
                canonical,
                stage.Envelope.CanonicalRelocationHigh,
                stage.Envelope.CanonicalRelocationLow,
                stage.TargetAttemptGeneration,
                stage.Node.Node.RoutingId,
                stage.TargetNodeLifecycleGeneration,
                targetOwner))
            throw new ZLinkRelocationDataLostException(
                "The canonical reply relay target attempt does not match durable authority.");
        return new ZLinkServiceWireCodec.ReplyRelayRecord(
            new MeshOperationId(
                completion.OperationHigh,
                completion.OperationLow),
            request.ReplyRouteId,
            new ZLinkServiceWireCodec.RelocationWireId(
                stage.Envelope.CanonicalRelocationHigh,
                stage.Envelope.CanonicalRelocationLow),
            canonical.TargetAttemptGeneration,
            new ZLinkServiceWireCodec.RelocationCoordinatorFence(
                targetOwner.OwnerId,
                checked((ulong)targetOwner.LeaseGeneration),
                stage.Node.Node.RoutingId,
                stage.TargetNodeLifecycleGeneration,
                found.Snapshot.StoreVersion),
            completion.ParticipantId,
            completion.AcceptedSequence,
            completion.TerminalResult,
            (ServiceWireConstants.FrameworkErrorCode)completion.ErrorCode);
    }

    internal static bool IsExactCanonicalReplyRelayTarget(
        ZLinkCanonicalRelocationAuthorityProjection canonical,
        ulong relocationHigh,
        ulong relocationLow,
        ulong targetAttemptGeneration,
        RoutingId targetNodeRid,
        ulong targetNodeGeneration,
        ZLinkLocationOwnerToken targetOwner) =>
        targetAttemptGeneration != 0
        && canonical.TargetAttemptGeneration == targetAttemptGeneration
        && canonical.RelocationHigh == relocationHigh
        && canonical.RelocationLow == relocationLow
        && canonical.State.TargetNodeRid == targetNodeRid.ToHex()
        && canonical.State.TargetNodeGeneration == targetNodeGeneration
        && canonical.TargetOwnerId == targetOwner.OwnerId
        && targetOwner.LeaseGeneration > 0
        && canonical.TargetOwnerLeaseGeneration
           == checked((ulong)targetOwner.LeaseGeneration);

    internal static bool IsExactSourceLeaseExpired(
        ZLinkOwnerLeaseReadResult lease,
        ZLinkCanonicalTerminalCompletion completion) =>
        lease switch
        {
            ZLinkOwnerLeaseReadResult.Missing => true,
            ZLinkOwnerLeaseReadResult.Found found =>
                completion.SourceOwnerLeaseGeneration > long.MaxValue
                || !StringComparer.Ordinal.Equals(
                    found.Token.OwnerId,
                    completion.SourceOwnerId)
                || found.Token.LeaseGeneration
                   != checked((long)completion.SourceOwnerLeaseGeneration)
                || found.LeaseExpiresAt <= found.StoreNow,
            _ => false
        };

    private static ZLinkCanonicalApplicationPayload DecodeCanonicalReply(
        IReadOnlyList<byte[]> replyParts)
    {
        var parts = replyParts.Select(static part => Message.From(part)).ToArray();
        try
        {
            var header = ZLinkEnvelopeCodec.DecodeHeader(parts);
            if (parts.Length != 2)
                throw new ZLinkRelocationDataLostException(
                    "Canonical terminal reply must contain header and body parts.");
            return new ZLinkCanonicalApplicationPayload(
                header.MessageName,
                header.ContentType,
                parts[1].ToArray());
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }
    }

    private static byte[][] EncodeCanonicalReply(
        string channelName,
        ZLinkCanonicalAcceptedRequest request,
        ZLinkCanonicalTerminalCompletion completion)
    {
        var payload = completion.Payload
                      ?? throw new ZLinkRelocationDataLostException(
                          "Canonical terminal reply has no application payload.");
        using var header = ZLinkEnvelopeCodec.EncodeHeader(
            new ZLinkEnvelopeHeader(
                ZLinkMessageKind.Response,
                channelName,
                payload.PacketName,
                payload.ContentType,
                request.ReplyRouteId.ToString(
                    System.Globalization.CultureInfo.InvariantCulture),
                null,
                null,
                null,
                null));
        return [header.ToArray(), payload.Payload.ToArray()];
    }

    internal async ValueTask AbortInboundSpotAggregateAsync(TargetStage stage)
    {
        if (Volatile.Read(ref stage.Published) != 0)
            return;
        foreach (var actorState in stage.ActorStates)
            await _actorSessionManager.RollbackTransferredActorAsync(
                    actorState.ActorId,
                    CancellationToken.None)
                .ConfigureAwait(false);
        await stage.Node.Catalog.DiscardReservedAsync(stage.Spot)
            .ConfigureAwait(false);
    }

    internal ValueTask RelayCommittedSpotRecordsAsync(
        ZLinkSpotRetireReservation reservation,
        ZLinkAggregateRelocationPublished relocation,
        IReadOnlyList<ZLinkAcceptedWorkRecord> held,
        CancellationToken cancellationToken)
    {
        // The accepted queue, including ingress held at the cutoff, is already
        // part of the one immutable root. Command 31 transfers that journal;
        // no second network relay is allowed after authority publication.
        cancellationToken.ThrowIfCancellationRequested();
        _ = reservation;
        _ = relocation;
        _ = held;
        return ValueTask.CompletedTask;
    }
}
