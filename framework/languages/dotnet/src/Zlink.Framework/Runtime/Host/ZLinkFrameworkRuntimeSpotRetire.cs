namespace Zlink.Framework.Runtime.Host;

internal sealed partial class ZLinkFrameworkRuntime
{
    internal bool TryAcquireInboundSpotRelocation(
        long payloadBytes,
        bool restore,
        bool allowOversizedPayload,
        out IDisposable permit)
    {
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

    internal SubmitResult TryRelaySpotReplyOnce(
        string sourceMeshName,
        RoutingId sourceNodeRid,
        ZLinkSpotRelocationReplyRelay relay)
    {
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            sourceMeshName,
            ZLinkSpotRelocationReplyRelayProtocol.PacketName);
        var parts = ZLinkEnvelopeCodec.EncodeParts(
            header,
            relay,
            typeof(ZLinkSpotRelocationReplyRelay),
            Registration.Codecs);
        try
        {
            return GetMeshNodeRuntime(sourceMeshName).Node.SendToNode(
                sourceNodeRid,
                parts,
                SendFlags.DontWait);
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }
    }

    internal async ValueTask<TargetStage> StageInboundSpotAggregateAsync(
        ZLinkSpotRetireStageRequest request,
        ZLinkRelocationEnvelope envelope,
        IDisposable inboundPermit,
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
                spotParticipant.AuthorityOwnerGeneration,
                targetOwnerGeneration,
                request.MeshName,
                request.TargetNodeLifecycleGeneration,
                request.TargetOwnerLeaseGeneration);
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

            await normalizeAuthority(cancellationToken)
                .ConfigureAwait(false);
            if (Volatile.Read(ref stage.LocalCatalogPublished) == 0)
            {
                foreach (var actorState in stage.ActorStates)
                    _actorSessionManager.PublishReservedActor(
                        actorState.ActorId);
                stage.Node.Catalog.PublishRelocatedReserved(stage.Spot);
                Volatile.Write(ref stage.LocalCatalogPublished, 1);
            }
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

    private static async ValueTask PrepareInboundSpotAggregateCoreAsync(
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
        if (Volatile.Read(ref stage.JobsStaged) != 0)
            return;
        var replayJobs = spotParticipant.AcceptedJobs
            .Concat(stage.HeldRecords)
            .OrderBy(static job => job.AcceptedSequence)
            .ToArray();
        await stage.Spot.Activation.ReplayAcceptedJobsAsync(
                replayJobs,
                stage.SourceMeshName,
                stage.TargetAuthorityOwnerGeneration,
                stage.TargetAdmissionSeal,
                Volatile.Read(ref stage.ReplayedJobCount),
                () => Interlocked.Increment(ref stage.ReplayedJobCount),
                cancellationToken)
            .ConfigureAwait(false);
        if (Volatile.Read(ref stage.ReplayedJobCount) != replayJobs.Length)
            throw new ZLinkRelocationDataLostException(
                $"Target SPOT '{stage.Spot.Activation.SpotId}' did not complete every accepted replay.");
        Volatile.Write(ref stage.JobsStaged, 1);
    }

    internal async ValueTask AbortInboundSpotAggregateAsync(TargetStage stage)
    {
        if (Volatile.Read(ref stage.Published) != 0)
            return;
        foreach (var actorState in stage.ActorStates)
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
        await stage.Node.Catalog.DiscardReservedAsync(stage.Spot)
            .ConfigureAwait(false);
    }

    internal async ValueTask RelayCommittedSpotRecordsAsync(
        ZLinkSpotRetireReservation reservation,
        ZLinkAggregateRelocationPublished relocation,
        IReadOnlyList<ZLinkAcceptedWorkRecord> held,
        CancellationToken cancellationToken)
    {
        var records = held
            .OrderBy(static record => record.AcceptedSequence)
            .Select(static record => new ZLinkSpotRetireHeldRecord(
                record.AcceptedSequence,
                record.Payload.ToArray()))
            .ToArray();
        ZLinkSpotRetireTargetRuntime.ValidateHeldRecords(records);
        var spotParticipant = relocation.Envelope.Participants.Single(
            static participant => participant.ObjectKind
                is ZLinkPlacementObjectKind.UserSpot
                or ZLinkPlacementObjectKind.InstanceSpot);
        var reply = await RouteClient.RequestToNode(
                reservation.TargetDescriptor.MeshName,
                reservation.TargetDescriptor.Rid,
                new ZLinkSpotRetireHeldRelay(
                    relocation.Fence.AggregateId,
                    relocation.Fence.AggregateGeneration,
                    reservation.Inventory.SpotId,
                    spotParticipant.ObjectGeneration,
                    spotParticipant.AuthorityOwnerGeneration,
                    checked(spotParticipant.AuthorityOwnerGeneration + 1),
                    reservation.Inventory.SourceNodeLifecycleGeneration,
                    reservation.Inventory.SourceOwner.OwnerId,
                    checked((ulong)reservation.Inventory.SourceOwner
                        .LeaseGeneration),
                    reservation.TargetDescriptorLifecycleGeneration,
                    reservation.TargetOwner.OwnerId,
                    checked((ulong)reservation.TargetOwner.LeaseGeneration),
                    HopCount: 1,
                    records))
            .Async<ZLinkSpotRetireReply>(cancellationToken)
            .ConfigureAwait(false);
        if (!reply.Acknowledged)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SpotMoving,
                $"Target did not durably accept held ingress for SPOT '{reservation.Inventory.SpotId}'.",
                true);
    }
}
