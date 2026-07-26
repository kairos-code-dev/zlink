using System.Collections.Concurrent;
using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Locations;
using Zlink.Framework.Runtime.Service;

namespace Zlink.Framework.Runtime.Host;

/// <summary>
/// Owns maintenance relocation for Actors that are attached to an Entry Spot.
/// Application join admission is deliberately outside this module: a maintenance
/// relocation restores an existing Actor under an exact authority fence and is
/// not a new application-level join.
/// </summary>
internal sealed class ZLinkStandaloneActorRelocationRuntime(
    ZLinkFrameworkRuntime runtime,
    ZLinkActorSessionManager actorSessions,
    ZLinkFrameworkRegistration registration)
{
    private const int MaximumTargetStages = 1024;
    private static readonly TimeSpan TargetStageTtl = TimeSpan.FromMinutes(5);
    private readonly ConcurrentDictionary<AttemptKey, TargetStage> _targetStages = new();
    private readonly ConcurrentDictionary<AttemptKey, SemaphoreSlim> _targetStageGates = new();

    internal async ValueTask<bool> RelocateSourceAsync(
        ZLinkActorRuntimeState actorState,
        ZLinkMeshNodeDescriptor target,
        CancellationToken cancellationToken)
    {
        if (actorState.LiveActivation is not null)
            throw new InvalidOperationException(
                "User Spot member Actors are relocated by their aggregate owner.");
        var actor = actorState.Actor;
        var sourceRef = actorState.NativeActorRef;
        var actorType = actorState.ActorType;
        if (actor is null || sourceRef is null
            || string.IsNullOrWhiteSpace(actorType))
            return false;
        if (target.Rid == sourceRef.Value.NodeRid)
            return false;
        ZLinkActorRelocationRegistry.TryResolve(
            registration,
            actorType,
            sourceRef.Value.NodeRid,
            out var relocation);
        if (relocation is null || relocation.PolicyKind == 0)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RelocationDisabled,
                $"Relocation is disabled for Actor '{actorState.ActorId}'.");

        var authorityStore = registration.Locations.ResolveStore()
                             ?? throw new ZLinkConfigurationException(
                                 "Standalone Actor relocation requires a Location Store.");
        var relocationStore = registration.Locations.RelocationStoreInstance
                              ?? throw new ZLinkConfigurationException(
                                  "Standalone Actor relocation requires a Relocation Store.");
        var read = await authorityStore.ReadAuthorityAsync(
                ZLinkActorAuthorityPayloadCodec.AuthorityKey(actorState.ActorId),
                cancellationToken)
            .ConfigureAwait(false);
        if (read is not ZLinkAuthorityReadResult.Found found
            || found.Snapshot.ObjectGeneration != sourceRef.Value.Generation
            || !ZLinkActorAuthorityPayloadCodec.TryDecodeRelocating(
                found.Snapshot.Payload.Span,
                out var sourceAuthority)
            || sourceAuthority.NodeRid != sourceRef.Value.NodeRid)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorGenerationStale,
                $"Actor '{actorState.ActorId}' authority changed before maintenance relocation.");

        var predictedBytes = relocation.PolicyKind == 2
            ? ZLinkRemoteActorJoinPackets.SnapshotApplicationStateReservationBytes
            : 64L * 1024;
        if (!runtime.RelocationPermits.TryAcquire(
                ZLinkRelocationPermitRequest.Outbound(
                    predictedBytes,
                    capture: relocation.PolicyKind == 2),
                out var sourcePermit))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorMoving,
                $"Actor '{actorState.ActorId}' source relocation admission is busy.",
                true);
        using (sourcePermit)
        {
            var relocationId = Guid.NewGuid();
            var handoffId = relocationId.ToString("N");
            var captureStarted = false;
            var committed = false;
            ZLinkActorBoundSession? sealedSession = null;
            ZLinkPreparedRelocation? prepared = null;
            IZLinkBackendCanonicalRelocationReservation? canonical = null;
            ZLinkServiceWireCodec.RelocationPrepareRecord? prepare = null;
            var acceptedCount = 0;
            try
            {
                var route = default(ZLinkRemoteActorBoundSessionRoute);
                if (actorState.TryGetBoundSession(out var bound))
                {
                    sealedSession = await SealSessionRouteAsync(
                            actorState,
                            bound,
                            handoffId,
                            cancellationToken)
                        .ConfigureAwait(false);
                    route = ToRemoteRoute(sealedSession.Value);
                }

                _ = await actorState.BeginHandoffCaptureAsync(cancellationToken)
                    .ConfigureAwait(false);
                captureStarted = true;
                var applicationState = await ZLinkActorRelocationRegistry.CaptureAsync(
                        runtime.Services,
                        relocation,
                        actor,
                        cancellationToken)
                    .ConfigureAwait(false);
                if (applicationState.Length
                    > ZLinkRemoteActorJoinPackets
                        .SnapshotApplicationStateReservationBytes)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.RequestRejected,
                        $"Actor '{actorState.ActorId}' relocation state exceeds 64 MiB.");
                var accepted = actorState.Handoff.SnapshotFrames();
                acceptedCount = accepted.Count;
                var envelope = CreateImmutableRoot(
                    found.Snapshot,
                    sourceAuthority,
                    target,
                    relocationId,
                    applicationState,
                    accepted,
                    route);
                var publication = new ZLinkRelocationPublicationCoordinator(
                    authorityStore,
                    relocationStore);
                prepared = await publication.PrepareAsync(
                        envelope,
                        cancellationToken)
                    .ConfigureAwait(false);
                var encodedBytes = ZLinkRelocationEnvelopeCodec
                    .MeasureEncodedLength(envelope);
                if (!sourcePermit.TryShrinkPayload(encodedBytes))
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.RequestRejected,
                        $"Actor '{actorState.ActorId}' relocation root exceeded its reservation.");

                var sourceNode = runtime.GetSpotNodeRuntime(sourceRef.Value.NodeRid);
                if (sourceNode.Node
                    is not IZLinkBackendCanonicalRelocationReservation backend)
                    throw new ZLinkConfigurationException(
                        "The source MeshNode does not support canonical relocation commands.");
                canonical = backend;
                prepare = CreatePrepare(
                    found.Snapshot,
                    sourceAuthority,
                    target,
                    envelope,
                    prepared.Relocation,
                    sealedSession,
                    registration.ApplicationVersion);
                _ = await canonical.ReserveCanonicalRelocationAsync(
                        target.Rid,
                        prepare,
                        registration.DefaultRequestTimeout,
                        cancellationToken)
                    .ConfigureAwait(false);
                var data = envelope.Participants[0].AcceptedJobs
                    .Select((job, index) =>
                        new ZLinkServiceWireCodec.RelocationDataRecord(
                            prepare.RelocationId,
                            prepare.TargetAttemptGeneration,
                            prepare.Coordinator,
                            1,
                            1,
                            checked((ulong)index + 1),
                            new ZLinkServiceWireCodec.FrozenRecord(
                                job.Payload.ToArray())))
                    .ToArray();
                await canonical.StageCanonicalRelocationAsync(
                        target.Rid,
                        prepare,
                        data,
                        registration.DefaultRequestTimeout,
                        cancellationToken)
                    .ConfigureAwait(false);
                committed = true;
                await CompleteCommittedSourceAsync(
                        actor,
                        actorState,
                        sourceRef.Value,
                        sourceAuthority,
                        found.Snapshot,
                        target,
                        relocationId,
                        acceptedCount,
                        canonical,
                        prepare)
                    .ConfigureAwait(false);
                return true;
            }
            catch
            {
                if (!committed)
                {
                    if (canonical is not null && prepare is not null)
                        canonical.CancelCanonicalRelocation(
                            target.Rid,
                            prepare.RelocationId,
                            prepare.TargetAttemptGeneration,
                            prepare.Coordinator);
                    var authority = await authorityStore.ReadAuthorityAsync(
                            ZLinkActorAuthorityPayloadCodec.AuthorityKey(
                                actorState.ActorId),
                            CancellationToken.None)
                        .ConfigureAwait(false);
                    if (!IsExactSourceAuthority(
                            authority,
                            found.Snapshot,
                            sourceAuthority))
                    {
                        if (prepared is null
                            || canonical is null
                            || prepare is null
                            || !IsExactCommittedTargetAuthority(
                                authority,
                                found.Snapshot,
                                prepared.Relocation,
                                relocationId,
                                target))
                            throw DataLost(
                                $"Actor '{actorState.ActorId}' authority changed to an unrelated owner during relocation.");

                        // Command 34 can be lost after the target CAS. Continue
                        // the exact committed attempt instead of reopening or
                        // selecting another target.
                        committed = true;
                        await CompleteCommittedSourceAsync(
                                actor,
                                actorState,
                                sourceRef.Value,
                                sourceAuthority,
                                found.Snapshot,
                                target,
                                relocationId,
                                acceptedCount,
                                canonical,
                                prepare)
                            .ConfigureAwait(false);
                        return true;
                    }
                    if (prepared is not null)
                        await new ZLinkRelocationPublicationCoordinator(
                                authorityStore,
                                relocationStore)
                            .DiscardPreparedAsync(prepared)
                            .ConfigureAwait(false);
                    if (captureStarted)
                        await runtime.RestoreStandaloneActorRelocationSourceAsync(
                                actorState)
                            .ConfigureAwait(false);
                    if (sealedSession is { } session)
                        await AbortSessionRouteBestEffortAsync(
                                actorState.ActorId,
                                session,
                                handoffId)
                            .ConfigureAwait(false);
                }
                throw;
            }
        }
    }

    private static bool IsExactSourceAuthority(
        ZLinkAuthorityReadResult current,
        ZLinkAuthoritySnapshot expected,
        ZLinkActorAuthorityPayload expectedPayload)
    {
        return current is ZLinkAuthorityReadResult.Found found
               && found.Snapshot.StoreVersion == expected.StoreVersion
               && found.Snapshot.ObjectGeneration == expected.ObjectGeneration
               && found.Snapshot.AuthorityOwnerGeneration
               == expected.AuthorityOwnerGeneration
               && found.Snapshot.OwnerId == expected.OwnerId
               && found.Snapshot.OwnerLeaseGeneration
               == expected.OwnerLeaseGeneration
               && ZLinkActorAuthorityPayloadCodec.TryDecodeRelocating(
                   found.Snapshot.Payload.Span,
                   out var payload)
               && payload == expectedPayload;
    }

    private static bool IsExactCommittedTargetAuthority(
        ZLinkAuthorityReadResult current,
        ZLinkAuthoritySnapshot source,
        ZLinkRelocationStored root,
        Guid relocationId,
        ZLinkMeshNodeDescriptor target) =>
        current is ZLinkAuthorityReadResult.Found found
        && found.Snapshot.ObjectGeneration == source.ObjectGeneration
        && found.Snapshot.AuthorityOwnerGeneration
        == checked(source.AuthorityOwnerGeneration + 1)
        && found.Snapshot.OwnerId == target.OwnerId
        && found.Snapshot.OwnerLeaseGeneration == target.LeaseGeneration
        && ZLinkRelocationAuthorityPayloadCodec.TryDecode(
            found.Snapshot.Payload.Span,
            out var publication)
        && publication.AggregateId == relocationId
        && StringComparer.Ordinal.Equals(publication.Reference, root.Reference)
        && publication.ChecksumCrc32c == root.ChecksumCrc32c
        && StringComparer.Ordinal.Equals(publication.TargetOwnerId, target.OwnerId)
        && publication.TargetOwnerLeaseGeneration == target.LeaseGeneration;

    private async ValueTask CompleteCommittedSourceAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef sourceRef,
        ZLinkActorAuthorityPayload sourceAuthority,
        ZLinkAuthoritySnapshot sourceSnapshot,
        ZLinkMeshNodeDescriptor target,
        Guid relocationId,
        int acceptedCount,
        IZLinkBackendCanonicalRelocationReservation canonical,
        ZLinkServiceWireCodec.RelocationPrepareRecord prepare)
    {
        var targetRef = new ZLinkBackendActorRef(
            target.Rid, actorState.ActorId, sourceRef.Generation);
        var ownership = ZLinkFrameworkRuntime
            .RequireActorRelocationLocationLifecycle(
                runtime.LocationLifecycle,
                actorState.ActorId)
            .ActorOwnership;
        await ownership.AdvanceTransferredActorAuthorityPhaseAsync(
                actorState.ActorId,
                targetRef.ToNative(sourceAuthority.MeshName),
                relocationId,
                ZLinkActorRelocationAuthorityPhase.Activated,
                ZLinkActorRelocationAuthorityPhase.Cleaning,
                CancellationToken.None)
            .ConfigureAwait(false);
        var trailing = actorState.Handoff.CutoverCaptureToForwarding(
            acceptedCount,
            sourceRef,
            targetRef,
            sourceAuthority.MeshName,
            sourceAuthority.NodeGeneration,
            target.LifecycleGeneration,
            sourceSnapshot.AuthorityOwnerGeneration,
            checked(sourceSnapshot.AuthorityOwnerGeneration + 1),
            sourceAuthority.OwnerLeaseGeneration,
            checked((ulong)target.LeaseGeneration));
        runtime.RelayStandaloneActorRelocationTrailing(
            actorState, sourceRef, trailing);
        await runtime.CompleteStandaloneActorRelocationSourceAsync(
                actor,
                actorState,
                sourceRef,
                targetRef,
                CancellationToken.None)
            .ConfigureAwait(false);
        actorState.Handoff.CommitForwardingCutover(
            registration.Locations.Options.RelocationForwardingWindow);
        await ownership.AdvanceTransferredActorAuthorityPhaseAsync(
                actorState.ActorId,
                targetRef.ToNative(sourceAuthority.MeshName),
                relocationId,
                ZLinkActorRelocationAuthorityPhase.Cleaning,
                ZLinkActorRelocationAuthorityPhase.Completed,
                CancellationToken.None)
            .ConfigureAwait(false);
        await canonical.CompleteCanonicalRelocationAsync(
                target.Rid,
                new ZLinkServiceWireCodec.RelocationCompleteRecord(
                    prepare.RelocationId,
                    prepare.TargetAttemptGeneration,
                    prepare.Coordinator,
                    1,
                    new ZLinkServiceWireCodec.RequestSourceFence(
                        prepare.Coordinator.OwnerId,
                        prepare.Coordinator.LeaseGeneration,
                        prepare.SourceNodeRid,
                        prepare.SourceNodeGeneration),
                    1),
                CancellationToken.None)
            .ConfigureAwait(false);
    }

    internal static ZLinkRelocationEnvelope CreateImmutableRoot(
        ZLinkAuthoritySnapshot sourceSnapshot,
        ZLinkActorAuthorityPayload sourceAuthority,
        ZLinkMeshNodeDescriptor target,
        Guid relocationId,
        ReadOnlyMemory<byte> applicationState,
        IReadOnlyList<ZLinkActorHandoffFrame> acceptedFrames,
        ZLinkRemoteActorBoundSessionRoute boundSessionRoute)
    {
        ArgumentNullException.ThrowIfNull(sourceSnapshot);
        ArgumentNullException.ThrowIfNull(sourceAuthority);
        ArgumentNullException.ThrowIfNull(target);
        if (relocationId == Guid.Empty
            || sourceSnapshot.ObjectGeneration == 0
            || sourceSnapshot.AuthorityOwnerGeneration == 0
            || sourceSnapshot.OwnerLeaseGeneration <= 0
            || sourceSnapshot.OwnerId != sourceAuthority.OwnerId
            || checked((ulong)sourceSnapshot.OwnerLeaseGeneration)
            != sourceAuthority.OwnerLeaseGeneration
            || sourceSnapshot.Allocation.Descriptor.Rid
            != sourceAuthority.NodeRid
            || sourceSnapshot.Allocation.DescriptorLifecycleGeneration
            != sourceAuthority.NodeGeneration
            || target.LifecycleGeneration == 0
            || target.LeaseGeneration <= 0
            || string.IsNullOrWhiteSpace(target.OwnerId)
            || string.IsNullOrWhiteSpace(target.EntrySpotId))
            throw new ArgumentOutOfRangeException(nameof(relocationId));

        var targetAuthority = sourceAuthority with
        {
            State = ZLinkActorAuthorityState.Ready,
            CurrentSpotId = target.EntrySpotId,
            CurrentSpotGeneration = target.LifecycleGeneration,
            CurrentSpotKind = ZLinkSpotKind.Entry,
            OwnerId = target.OwnerId,
            OwnerLeaseGeneration = checked((ulong)target.LeaseGeneration),
            MeshName = target.MeshName,
            NodeRid = target.Rid,
            NodeGeneration = target.LifecycleGeneration
        };
        var relocatingAuthority = ZLinkActorRelocationAuthorityPayloadCodec.Encode(
            new ZLinkActorRelocationAuthorityPayload(
                relocationId,
                ZLinkActorRelocationAuthorityPhase.Activated,
                boundSessionRoute,
                ZLinkActorAuthorityPayloadCodec.Encode(targetAuthority)));
        var sourceFence = new ZLinkServiceWireCodec.RequestSourceFence(
            sourceSnapshot.OwnerId,
            checked((ulong)sourceSnapshot.OwnerLeaseGeneration),
            sourceAuthority.NodeRid,
            sourceAuthority.NodeGeneration);
        var sourceActor = new ZLinkBackendActorRef(
            sourceAuthority.NodeRid,
            sourceAuthority.ActorId,
            sourceSnapshot.ObjectGeneration);
        var recovery = ZLinkCanonicalParticipantRecoveryCodec.Encode(
            new ZLinkCanonicalParticipantRecovery(
                ZLinkActorAuthorityPayloadCodec.AuthorityKey(
                    sourceAuthority.ActorId),
                ZLinkPlacementObjectKind.Actor,
                sourceSnapshot.ObjectGeneration,
                sourceSnapshot.AuthorityOwnerGeneration,
                sourceSnapshot.StoreVersion,
                sourceAuthority.StableType,
                relocatingAuthority,
                ZLinkActorRelocationSourceFenceCodec.Encode(
                    new ZLinkActorRelocationSourceFence(
                        sourceFence.OwnerId,
                        sourceFence.LeaseGeneration,
                        sourceFence.NodeRid,
                        sourceFence.NodeGeneration))));
        var participant = new ZLinkRelocationParticipantEnvelope(
            ZLinkActorAuthorityPayloadCodec.AuthorityKey(sourceAuthority.ActorId),
            ZLinkPlacementObjectKind.Actor,
            sourceSnapshot.ObjectGeneration,
            sourceSnapshot.AuthorityOwnerGeneration,
            applicationState,
            acceptedFrames
                .OrderBy(static frame => frame.ArrivalIndex)
                .Select((frame, index) => new ZLinkRelocationQueuedJob(
                    checked((ulong)index + 1),
                    ZLinkCanonicalActorAcceptedJournal.Encode(
                        frame,
                        sourceFence,
                        sourceActor)))
                .ToArray(),
            [],
            RecoveryPayload: recovery)
        {
            CanonicalParticipantId = 1,
            AcceptedBoundary = checked((ulong)acceptedFrames.Count)
        };
        var publicationParticipant = new ZLinkAggregateRelocationParticipant(
            participant,
            sourceSnapshot.StoreVersion,
            ZLinkAuthorityGenerationTransition.NewOwner,
            relocatingAuthority,
            ReadOnlyMemory<byte>.Empty);
        var digest = ZLinkAggregateInventoryDigest.Compute([publicationParticipant]);
        return new ZLinkRelocationEnvelope(
            relocationId,
            1,
            digest,
            [participant]);
    }

    private async ValueTask<ZLinkActorBoundSession> SealSessionRouteAsync(
        ZLinkActorRuntimeState actorState,
        ZLinkActorBoundSession session,
        string handoffId,
        CancellationToken cancellationToken)
    {
        var sourceRef = actorState.NativeActorRef
                        ?? throw new ZLinkFrameworkException(
                            ZLinkFrameworkErrorKind.ActorRouteNotFound,
                            $"Actor '{actorState.ActorId}' has no source reference.");
        var sessionNode = session.SessionNodeRid ?? sourceRef.NodeRid;
        var normalized = session with
        {
            SessionNodeRid = sessionNode,
            BindingGeneration = Math.Max(1, session.BindingGeneration),
            ObjectGeneration = sourceRef.Generation,
            AuthorityOwnerGeneration = session.AuthorityOwnerGeneration == 0
                ? throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorSessionNotBound,
                    $"Actor '{actorState.ActorId}' session authority fence is empty.")
                : session.AuthorityOwnerGeneration
        };
        var request = new ZLinkSessionRouteSealRequest(
            actorState.ActorId,
            normalized.BindingToken,
            normalized.BindingGeneration,
            normalized.ObjectGeneration,
            normalized.AuthorityOwnerGeneration,
            normalized.MeshName,
            normalized.TargetNodeGeneration,
            normalized.OwnerLeaseGeneration,
            normalized.SessionOwnerNodeGeneration,
            handoffId);
        ZLinkSessionRouteSealReply reply;
        if (sessionNode == runtime.GetMeshNodeRuntime(normalized.MeshName)
                .Node.RoutingId)
        {
            var result = await runtime.SealSessionActorRouteAsync(
                    new ZLinkSessionRouteSeal(
                        request.ActorId,
                        request.BindingToken,
                        request.BindingGeneration,
                        request.ObjectGeneration,
                        request.AuthorityOwnerGeneration,
                        request.MeshName,
                        request.TargetNodeGeneration,
                        request.OwnerLeaseGeneration,
                        request.SessionOwnerNodeGeneration,
                        request.HandoffId),
                    cancellationToken)
                .ConfigureAwait(false);
            reply = new ZLinkSessionRouteSealReply(
                result.Acknowledged,
                result.AcceptedHighWater);
        }
        else
        {
            var route = (IZLinkRouteClient?)runtime.Services.GetService(
                            typeof(IZLinkRouteClient))
                        ?? throw new InvalidOperationException(
                            "Route client service is unavailable during Actor maintenance.");
            reply = await route.RequestToNode(
                    normalized.MeshName,
                    sessionNode,
                    request)
                .Timeout(registration.DefaultRequestTimeout)
                .Async<ZLinkSessionRouteSealReply>(cancellationToken)
                .ConfigureAwait(false);
        }
        if (!reply.Acknowledged)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorSessionNotBound,
                $"Actor '{actorState.ActorId}' session ingress seal was fenced.");
        normalized = normalized with { AcceptedHighWater = reply.AcceptedHighWater };
        actorState.BindSession(
            normalized.SessionNodeRid,
            normalized.SessionRid,
            normalized.BindingToken,
            normalized.BindingGeneration,
            normalized.ObjectGeneration,
            normalized.AuthorityOwnerGeneration,
            normalized.MeshName,
            normalized.TargetNodeGeneration,
            normalized.OwnerLeaseGeneration,
            normalized.SessionOwnerNodeGeneration,
            normalized.AcceptedHighWater);
        return normalized;
    }

    private async ValueTask AbortSessionRouteBestEffortAsync(
        string actorId,
        ZLinkActorBoundSession session,
        string handoffId)
    {
        try
        {
            var seal = new ZLinkSessionRouteSeal(
                actorId,
                session.BindingToken,
                session.BindingGeneration,
                session.ObjectGeneration,
                session.AuthorityOwnerGeneration,
                session.MeshName,
                session.TargetNodeGeneration,
                session.OwnerLeaseGeneration,
                session.SessionOwnerNodeGeneration,
                handoffId);
            var sessionNode = session.SessionNodeRid!.Value;
            if (sessionNode == runtime.GetMeshNodeRuntime(session.MeshName)
                    .Node.RoutingId)
            {
                _ = runtime.AbortSessionActorRouteSeal(seal);
                return;
            }
            var route = (IZLinkRouteClient?)runtime.Services.GetService(
                typeof(IZLinkRouteClient));
            if (route is null) return;
            _ = await route.RequestToNode(
                    session.MeshName,
                    sessionNode,
                    new ZLinkSessionRouteAbortRequest(
                        actorId,
                        session.BindingToken,
                        session.BindingGeneration,
                        session.ObjectGeneration,
                        session.AuthorityOwnerGeneration,
                        session.MeshName,
                        session.TargetNodeGeneration,
                        session.OwnerLeaseGeneration,
                        session.SessionOwnerNodeGeneration,
                        handoffId))
                .Timeout(registration.DefaultRequestTimeout)
                .Async<ZLinkSessionRouteSealReply>(CancellationToken.None)
                .ConfigureAwait(false);
        }
        catch
        {
            // A disconnected or replaced exact binding owns its own cleanup.
        }
    }

    private static ZLinkRemoteActorBoundSessionRoute ToRemoteRoute(
        ZLinkActorBoundSession session) => new(
        session.SessionNodeRid,
        session.SessionRid,
        session.BindingToken,
        session.BindingGeneration,
        session.ObjectGeneration,
        session.AuthorityOwnerGeneration,
        session.MeshName,
        session.TargetNodeGeneration,
        session.OwnerLeaseGeneration,
        session.SessionOwnerNodeGeneration,
        session.AcceptedHighWater);

    private static ZLinkServiceWireCodec.RelocationPrepareRecord CreatePrepare(
        ZLinkAuthoritySnapshot sourceSnapshot,
        ZLinkActorAuthorityPayload sourceAuthority,
        ZLinkMeshNodeDescriptor target,
        ZLinkRelocationEnvelope envelope,
        ZLinkRelocationStored root,
        ZLinkActorBoundSession? boundSession,
        long applicationVersion)
    {
        var relocationId = ToWireId(envelope.AggregateId);
        var participant = envelope.Participants.Single();
        var acceptedBytes = participant.AcceptedJobs.Aggregate(
            0UL,
            static (sum, job) => checked(sum + (ulong)job.Payload.Length));
        return new ZLinkServiceWireCodec.RelocationPrepareRecord(
            relocationId,
            1,
            1,
            new ZLinkServiceWireCodec.RelocationCoordinatorFence(
                sourceSnapshot.OwnerId,
                checked((ulong)sourceSnapshot.OwnerLeaseGeneration),
                sourceAuthority.NodeRid,
                sourceAuthority.NodeGeneration,
                sourceSnapshot.StoreVersion),
            new ZLinkServiceWireCodec.RelocationCandidateRecord(
                target.Rid,
                target.LifecycleGeneration,
                target.OwnerId,
                checked((ulong)target.LeaseGeneration)),
            1,
            new ZLinkServiceWireCodec.RelocationObjectRecord(
                1,
                sourceAuthority.StableType,
                sourceAuthority.ActorId,
                sourceSnapshot.ObjectGeneration,
                sourceSnapshot.AuthorityOwnerGeneration),
            sourceAuthority.NodeRid,
            sourceAuthority.NodeGeneration,
            checked((ulong)participant.AcceptedJobs.Count),
            checked((ulong)ZLinkRelocationEnvelopeCodec.MeasureEncodedLength(
                envelope)),
            [
                new ZLinkServiceWireCodec.RelocationParticipantRecord(
                    1,
                    1,
                    boundSession?.SessionNodeRid ?? default,
                    boundSession?.SessionOwnerNodeGeneration ?? 0,
                    null,
                    0,
                    boundSession?.SessionRid ?? default,
                    boundSession?.BindingGeneration ?? 0,
                    checked((ulong)participant.AcceptedJobs.Count),
                    acceptedBytes)
            ],
            new ZLinkServiceWireCodec.RelocationRootRecord(
                root.Reference,
                root.ChecksumCrc32c),
            checked((ulong)applicationVersion));
    }

    private static ZLinkServiceWireCodec.RelocationWireId ToWireId(Guid value)
    {
        var id = value.ToByteArray();
        return new ZLinkServiceWireCodec.RelocationWireId(
            System.Buffers.Binary.BinaryPrimitives.ReadUInt64BigEndian(
                id.AsSpan(0, 8)),
            System.Buffers.Binary.BinaryPrimitives.ReadUInt64BigEndian(
                id.AsSpan(8, 8)));
    }

    internal async ValueTask StageTargetAsync(
        ZLinkServiceWireCodec.RelocationPrepareRecord prepare,
        RoutingId authenticatedSourceNodeRid,
        CancellationToken cancellationToken)
    {
        await SweepExpiredTargetStagesAsync().ConfigureAwait(false);
        var key = new AttemptKey(
            prepare.RelocationId.High,
            prepare.RelocationId.Low,
            prepare.TargetAttemptGeneration);
        if (!_targetStages.ContainsKey(key)
            && !_targetStageGates.ContainsKey(key)
            && checked(_targetStages.Count + _targetStageGates.Count)
               >= MaximumTargetStages)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestRejected,
                "Standalone Actor target staging capacity is exhausted.",
                isRetriable: true);
        var gate = _targetStageGates.GetOrAdd(
            key,
            static _ => new SemaphoreSlim(1, 1));
        await gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            await StageTargetCoreAsync(
                    prepare,
                    authenticatedSourceNodeRid,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            gate.Release();
        }
    }

    private async ValueTask SweepExpiredTargetStagesAsync()
    {
        var cutoff = TimeProvider.System.GetUtcNow() - TargetStageTtl;
        foreach (var pair in _targetStages)
        {
            var stage = pair.Value;
            if (stage.AuthorityPublished
                || stage.CreatedAt > cutoff
                || !_targetStages.TryRemove(pair))
                continue;
            _targetStageGates.TryRemove(pair.Key, out _);
            stage.ActorState.AbortRelocationSessionRoute(
                stage.Envelope.AggregateId.ToString("N"));
            await actorSessions.RollbackTransferredActorAsync(
                    stage.ActorState.ActorId,
                    CancellationToken.None)
                .ConfigureAwait(false);
        }

        foreach (var pair in _targetStageGates)
            if (!_targetStages.ContainsKey(pair.Key)
                && pair.Value.CurrentCount == 1)
                _targetStageGates.TryRemove(pair);
    }

    internal static bool OwnsRecovery(
        ZLinkRelocationRecoveryCandidate candidate)
    {
        if (candidate.Envelope.Participants.Count != 1
            || candidate.Authorities.Count != 1)
            return false;
        try
        {
            var recovery = ZLinkCanonicalParticipantRecoveryCodec.Decode(
                candidate.Envelope.Participants[0].RecoveryPayload.Span);
            return recovery.ObjectKind == ZLinkPlacementObjectKind.Actor;
        }
        catch (Exception exception) when (exception is InvalidDataException
                                          or EndOfStreamException)
        {
            return false;
        }
    }

    internal async ValueTask RecoverPublishedAsync(
        ZLinkRelocationRecoveryCandidate candidate,
        CancellationToken cancellationToken)
    {
        if (!OwnsRecovery(candidate))
            throw DataLost(
                "Standalone Actor recovery received a non-canonical root.");
        var participant = candidate.Envelope.Participants.Single();
        var authority = candidate.Authorities.Single();
        var recovery = ZLinkCanonicalParticipantRecoveryCodec.Decode(
            participant.RecoveryPayload.Span);
        var sourceFence = ZLinkActorRelocationSourceFenceCodec.Decode(
            recovery.MembershipMutation.Span);
        if (recovery.AuthorityKey != authority.Key
            || recovery.AuthorityKey != participant.AuthorityKey
            || recovery.ObjectGeneration != participant.ObjectGeneration
            || recovery.AuthorityOwnerGeneration
            != participant.AuthorityOwnerGeneration
            || authority.Snapshot.ObjectGeneration
            != participant.ObjectGeneration
            || authority.Snapshot.AuthorityOwnerGeneration
            != checked(participant.AuthorityOwnerGeneration + 1)
            || !ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                authority.Snapshot.Payload.Span,
                out var publication)
            || publication.AggregateId != candidate.Envelope.AggregateId
            || !ZLinkActorRelocationAuthorityPayloadCodec.TryDecode(
                publication.ApplicationPayload.Span,
                out var relocating)
            || relocating.RelocationId != candidate.Envelope.AggregateId
            || !ZLinkActorAuthorityPayloadCodec.TryDecodeRelocating(
                relocating.ApplicationPayload.Span,
                out var targetAuthority)
            || ZLinkActorAuthorityPayloadCodec.AuthorityKey(
                   targetAuthority.ActorId) != recovery.AuthorityKey
            || targetAuthority.StableType != recovery.StableType
            || targetAuthority.OwnerId != authority.Snapshot.OwnerId
            || checked((long)targetAuthority.OwnerLeaseGeneration)
            != authority.Snapshot.OwnerLeaseGeneration)
            throw DataLost(
                "Standalone Actor recovery root does not match its published authority.");

        if (relocating.Phase is ZLinkActorRelocationAuthorityPhase.Activated
            or ZLinkActorRelocationAuthorityPhase.Cleaning)
        {
            var authorityStore = registration.Locations.ResolveStore()
                                 ?? throw new ZLinkConfigurationException(
                                     "Location Store is not registered.");
            var sourceLease = await authorityStore.ReadOwnerLeaseAsync(
                    sourceFence.OwnerId,
                    cancellationToken)
                .ConfigureAwait(false);
            if (!IsExactSourceLeaseExpired(sourceLease, sourceFence))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorMoving,
                    $"Actor '{targetAuthority.ActorId}' source cleanup is not yet durable.",
                    isRetriable: true);
        }

        var localNode = runtime.TryGetSpotNodeRuntime(targetAuthority.NodeRid);
        if (localNode is null
            || localNode.Node.MeshStatus().LifecycleGeneration
            != targetAuthority.NodeGeneration)
            return;
        if (!localNode.Registration.ActorRelocations.TryGetValue(
                recovery.StableType,
                out var relocation)
            || relocation.PolicyKind == 0)
            throw DataLost(
                $"Actor type '{recovery.StableType}' cannot restore the published relocation.");
        if (!runtime.RelocationPermits.TryAcquire(
                ZLinkRelocationPermitRequest.Inbound(
                    ZLinkRelocationEnvelopeCodec.MeasureEncodedLength(
                        candidate.Envelope),
                    restore: relocation.PolicyKind == 2),
                out var recoveryPermit))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorMoving,
                $"Actor '{targetAuthority.ActorId}' recovery admission is busy.",
                isRetriable: true);
        using var permit = recoveryPermit;

        var actorState = actorSessions.GetOrCreateState(targetAuthority.ActorId);
        await actorSessions.PrepareForTransferredActivationAsync(
                actorState,
                cancellationToken)
            .ConfigureAwait(false);
        await actorSessions.RelocateAndBindActorAsync(
                targetAuthority.ActorId,
                targetAuthority.StableType,
                relocation,
                ZLinkActorRelocationRegistry.ValidateIncomingPayload(
                    relocation,
                    targetAuthority.StableType,
                    relocation.PolicyKind == 2
                        ? ZLinkRemoteActorJoinPackets
                            .SnapshotRelocationContentType
                        : ZLinkRemoteActorJoinPackets
                            .RecreateRelocationContentType,
                    participant.ApplicationState),
                participant.ObjectGeneration,
                authority.Snapshot.AuthorityOwnerGeneration,
                ZLinkActorClaimMode.StagedRelocation,
                publishActorRef: false,
                cancellationToken)
            .ConfigureAwait(false);

        var handoffId = candidate.Envelope.AggregateId.ToString("N");
        actorState.StageRelocationSessionRoute(
            handoffId,
            relocating.BoundSessionRoute);
        var frames = DecodeFrames(participant.AcceptedJobs, sourceFence);
        actorState.Handoff.BeginCanonicalMaintenanceImport(handoffId, frames);
        var actorRef = actorState.NativeActorRef
                       ?? throw DataLost(
                           "Standalone Actor recovery did not create a target reference.");
        actorState.MarkRelocationSessionAuthorityCommitted(
            handoffId,
            actorRef,
            authority.Snapshot.AuthorityOwnerGeneration,
            targetAuthority.MeshName,
            targetAuthority.NodeGeneration,
            targetAuthority.OwnerLeaseGeneration);
        if (!actorState.Handoff.IsAuthorityCommitted(handoffId))
            actorState.Handoff.MarkAuthorityCommitted(
                handoffId,
                participant.ObjectGeneration,
                actorRef.Generation);

        var ownership = ZLinkFrameworkRuntime
            .RequireActorRelocationLocationLifecycle(
                runtime.LocationLifecycle,
                actorState.ActorId)
            .ActorOwnership;
        var durable = await ownership.ReadTransferredActorAuthorityPhaseAsync(
                actorState.ActorId,
                actorRef.ToNative(targetAuthority.MeshName),
                cancellationToken)
            .ConfigureAwait(false);
        if (durable?.Phase.RelocationId != candidate.Envelope.AggregateId)
            throw DataLost(
                "Standalone Actor recovery lost its relocation phase.");
        if (durable.Value.Phase.Phase
            == ZLinkActorRelocationAuthorityPhase.Activated)
        {
            await ownership.AdvanceTransferredActorAuthorityPhaseAsync(
                    actorState.ActorId,
                    actorRef.ToNative(targetAuthority.MeshName),
                    candidate.Envelope.AggregateId,
                    ZLinkActorRelocationAuthorityPhase.Activated,
                    ZLinkActorRelocationAuthorityPhase.Cleaning,
                    cancellationToken)
                .ConfigureAwait(false);
            durable = await ownership.ReadTransferredActorAuthorityPhaseAsync(
                    actorState.ActorId,
                    actorRef.ToNative(targetAuthority.MeshName),
                    cancellationToken)
                .ConfigureAwait(false);
        }
        if (durable?.Phase.Phase
            == ZLinkActorRelocationAuthorityPhase.Cleaning)
            await ownership.AdvanceTransferredActorAuthorityPhaseAsync(
                    actorState.ActorId,
                    actorRef.ToNative(targetAuthority.MeshName),
                    candidate.Envelope.AggregateId,
                    ZLinkActorRelocationAuthorityPhase.Cleaning,
                    ZLinkActorRelocationAuthorityPhase.Completed,
                    cancellationToken)
                .ConfigureAwait(false);

        await runtime.CompleteStandaloneActorRelocationTargetAsync(
                actorState,
                targetAuthority,
                handoffId,
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask StageTargetCoreAsync(
        ZLinkServiceWireCodec.RelocationPrepareRecord prepare,
        RoutingId authenticatedSourceNodeRid,
        CancellationToken cancellationToken)
    {
        ValidatePrepare(prepare, authenticatedSourceNodeRid);
        var key = new AttemptKey(
            prepare.RelocationId.High,
            prepare.RelocationId.Low,
            prepare.TargetAttemptGeneration);
        if (_targetStages.TryGetValue(key, out var existing))
        {
            existing.ValidateRetry(prepare, authenticatedSourceNodeRid);
            return;
        }

        var root = prepare.Root
                   ?? throw DataLost("Standalone Actor relocation has no immutable root.");
        var relocationStore = registration.Locations.RelocationStoreInstance
                              ?? throw new ZLinkConfigurationException(
                                  "Relocation Store is not registered.");
        var tree = await ZLinkRelocationTreeStore.ReadAsync(
                relocationStore,
                root.Reference,
                root.ChecksumCrc32c,
                cancellationToken)
            .ConfigureAwait(false);
        var participant = tree.Envelope.Participants.SingleOrDefault()
                          ?? throw DataLost(
                              "Standalone Actor relocation must contain one participant.");
        var recovery = ZLinkCanonicalParticipantRecoveryCodec.Decode(
            participant.RecoveryPayload.Span);
        if (ToWireId(tree.Envelope.AggregateId) != prepare.RelocationId
            || tree.Envelope.AggregateGeneration != 1
            || participant.ObjectKind != ZLinkPlacementObjectKind.Actor
            || recovery.ObjectKind != ZLinkPlacementObjectKind.Actor
            || recovery.AuthorityKey != participant.AuthorityKey
            || recovery.ObjectGeneration != participant.ObjectGeneration
            || recovery.AuthorityOwnerGeneration
               != participant.AuthorityOwnerGeneration
            || ZLinkActorAuthorityPayloadCodec.AuthorityKey(
                   prepare.Object.ObjectId) != recovery.AuthorityKey
            || prepare.Object.ObjectGeneration != participant.ObjectGeneration
            || prepare.Object.ExpectedAuthorityOwnerGeneration
               != participant.AuthorityOwnerGeneration
            || !ZLinkActorRelocationAuthorityPayloadCodec.TryDecode(
                recovery.AuthorityPayload.Span,
                out var relocating)
            || relocating.RelocationId != tree.Envelope.AggregateId
            || relocating.Phase != ZLinkActorRelocationAuthorityPhase.Activated
            || !ZLinkActorAuthorityPayloadCodec.TryDecodeRelocating(
                relocating.ApplicationPayload.Span,
                out var targetAuthority)
            || targetAuthority.ActorId != prepare.Object.ObjectId
            || targetAuthority.StableType != recovery.StableType
            || targetAuthority.NodeRid != prepare.Candidate.NodeRid
            || targetAuthority.NodeGeneration != prepare.Candidate.NodeGeneration
            || targetAuthority.OwnerId != prepare.Candidate.OwnerId
            || targetAuthority.OwnerLeaseGeneration
               != prepare.Candidate.OwnerLeaseGeneration)
            throw DataLost(
                "Standalone Actor relocation root does not match command 40.");

        var node = runtime.GetSpotNodeRuntime(prepare.Candidate.NodeRid);
        if (!node.Registration.ActorRelocations.TryGetValue(
                recovery.StableType,
                out var relocation)
            || relocation.PolicyKind == 0)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestRejected,
                $"Actor type '{recovery.StableType}' relocation is not registered on the target.");

        var actorState = actorSessions.GetOrCreateState(targetAuthority.ActorId);
        await actorSessions.PrepareForTransferredActivationAsync(
                actorState,
                cancellationToken)
            .ConfigureAwait(false);
        var created = false;
        try
        {
            var creation = await actorSessions.RelocateAndBindActorAsync(
                    targetAuthority.ActorId,
                    targetAuthority.StableType,
                    relocation,
                    ZLinkActorRelocationRegistry.ValidateIncomingPayload(
                        relocation,
                        targetAuthority.StableType,
                        relocation.PolicyKind == 2
                            ? ZLinkRemoteActorJoinPackets
                                .SnapshotRelocationContentType
                            : ZLinkRemoteActorJoinPackets
                                .RecreateRelocationContentType,
                        participant.ApplicationState),
                    participant.ObjectGeneration,
                    checked(participant.AuthorityOwnerGeneration + 1),
                    ZLinkActorClaimMode.StagedRelocation,
                    publishActorRef: false,
                    cancellationToken)
                .ConfigureAwait(false);
            created = creation.Created;
            actorState.StageRelocationSessionRoute(
                tree.Envelope.AggregateId.ToString("N"),
                relocating.BoundSessionRoute);
            var sourceFence = ZLinkActorRelocationSourceFenceCodec.Decode(
                recovery.MembershipMutation.Span);
            var frames = DecodeFrames(participant.AcceptedJobs, sourceFence);
            actorState.Handoff.BeginCanonicalMaintenanceImport(
                tree.Envelope.AggregateId.ToString("N"),
                frames);
            var stage = new TargetStage(
                prepare,
                authenticatedSourceNodeRid,
                tree.Envelope,
                actorState,
                targetAuthority,
                frames);
            if (!_targetStages.TryAdd(key, stage))
            {
                var winner = _targetStages[key];
                winner.ValidateRetry(prepare, authenticatedSourceNodeRid);
                await actorSessions.RollbackTransferredActorAsync(
                        targetAuthority.ActorId,
                        CancellationToken.None)
                    .ConfigureAwait(false);
            }
        }
        catch
        {
            if (created)
                await actorSessions.RollbackTransferredActorAsync(
                        targetAuthority.ActorId,
                        CancellationToken.None)
                    .ConfigureAwait(false);
            throw;
        }
    }

    internal void MarkAuthorityPublished(
        ZLinkServiceWireCodec.RelocationPrepareRecord prepare)
    {
        var key = new AttemptKey(
            prepare.RelocationId.High,
            prepare.RelocationId.Low,
            prepare.TargetAttemptGeneration);
        if (!_targetStages.TryGetValue(key, out var stage))
            throw DataLost("Standalone Actor target staging is unavailable.");
        if (stage.AuthorityPublished) return;
        var actorRef = stage.ActorState.NativeActorRef
                       ?? throw DataLost(
                           "Standalone Actor target native reference is unavailable.");
        stage.ActorState.MarkRelocationSessionAuthorityCommitted(
            stage.Envelope.AggregateId.ToString("N"),
            actorRef,
            checked(stage.Participant.AuthorityOwnerGeneration + 1),
            stage.TargetAuthority.MeshName,
            stage.TargetAuthority.NodeGeneration,
            stage.TargetAuthority.OwnerLeaseGeneration);
        if (!stage.ActorState.Handoff.IsAuthorityCommitted(
                stage.Envelope.AggregateId.ToString("N")))
            stage.ActorState.Handoff.MarkAuthorityCommitted(
                stage.Envelope.AggregateId.ToString("N"),
                stage.Participant.ObjectGeneration,
                actorRef.Generation);
        stage.AuthorityPublished = true;
    }

    internal async ValueTask CompleteTargetAsync(
        ZLinkServiceWireCodec.RelocationCompleteRecord complete,
        RoutingId authenticatedSourceNodeRid,
        CancellationToken cancellationToken)
    {
        var key = new AttemptKey(
            complete.RelocationId.High,
            complete.RelocationId.Low,
            complete.TargetAttemptGeneration);
        if (!_targetStages.TryGetValue(key, out var stage)) return;
        if (stage.SourceNodeRid != authenticatedSourceNodeRid
            || complete.Coordinator != stage.Prepare.Coordinator
            || !stage.AuthorityPublished)
            throw DataLost("Standalone Actor completion fence changed.");

        var handoffId = stage.Envelope.AggregateId.ToString("N");
        await runtime.CompleteStandaloneActorRelocationTargetAsync(
                stage.ActorState,
                stage.TargetAuthority,
                handoffId,
                cancellationToken)
            .ConfigureAwait(false);
        _targetStages.TryRemove(
            new KeyValuePair<AttemptKey, TargetStage>(key, stage));
        _targetStageGates.TryRemove(key, out _);
    }

    internal async ValueTask ReconcilePublishedTargetAsync(
        ZLinkServiceWireCodec.RelocationPrepareRecord prepare,
        CancellationToken cancellationToken)
    {
        var key = new AttemptKey(
            prepare.RelocationId.High,
            prepare.RelocationId.Low,
            prepare.TargetAttemptGeneration);
        if (!_targetStages.TryGetValue(key, out var stage)
            || !stage.AuthorityPublished)
            return;
        var store = registration.Locations.ResolveStore()
                    ?? throw new ZLinkConfigurationException(
                        "Location Store is not registered.");
        var read = await store.ReadAuthorityAsync(
                stage.Participant.AuthorityKey,
                cancellationToken)
            .ConfigureAwait(false);
        if (read is not ZLinkAuthorityReadResult.Found found
            || !ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                found.Snapshot.Payload.Span,
                out var publication)
            || publication.AggregateId != stage.Envelope.AggregateId
            || !ZLinkActorRelocationAuthorityPayloadCodec.TryDecode(
                publication.ApplicationPayload.Span,
                out var relocating))
            throw DataLost(
                "Standalone Actor target reconciliation lost its authority.");
        if (relocating.Phase == ZLinkActorRelocationAuthorityPhase.Steady)
        {
            _targetStages.TryRemove(
                new KeyValuePair<AttemptKey, TargetStage>(key, stage));
            _targetStageGates.TryRemove(key, out _);
            return;
        }
        if (relocating.Phase != ZLinkActorRelocationAuthorityPhase.Completed)
            return;

        await runtime.CompleteStandaloneActorRelocationTargetAsync(
                stage.ActorState,
                stage.TargetAuthority,
                stage.Envelope.AggregateId.ToString("N"),
                cancellationToken)
            .ConfigureAwait(false);
        _targetStages.TryRemove(
            new KeyValuePair<AttemptKey, TargetStage>(key, stage));
        _targetStageGates.TryRemove(key, out _);
    }

    internal async ValueTask AbortTargetAsync(
        ZLinkServiceWireCodec.RelocationPrepareRecord prepare)
    {
        var key = new AttemptKey(
            prepare.RelocationId.High,
            prepare.RelocationId.Low,
            prepare.TargetAttemptGeneration);
        if (!_targetStages.TryGetValue(key, out var stage)
            || stage.AuthorityPublished
            || !_targetStages.TryRemove(
                new KeyValuePair<AttemptKey, TargetStage>(key, stage)))
            return;
        _targetStageGates.TryRemove(key, out _);
        stage.ActorState.AbortRelocationSessionRoute(
            stage.Envelope.AggregateId.ToString("N"));
        await actorSessions.RollbackTransferredActorAsync(
                stage.ActorState.ActorId,
                CancellationToken.None)
            .ConfigureAwait(false);
    }

    private static ZLinkActorHandoffFrame[] DecodeFrames(
        IReadOnlyList<ZLinkRelocationQueuedJob> jobs,
        ZLinkActorRelocationSourceFence source)
    {
        try
        {
            var expected = new ZLinkServiceWireCodec.RequestSourceFence(
                source.OwnerId,
                source.OwnerLeaseGeneration,
                source.NodeRid,
                source.NodeGeneration);
            return jobs.OrderBy(static job => job.AcceptedSequence)
                .Select(job => ZLinkCanonicalActorAcceptedJournal.Decode(
                        job.Payload.Span,
                        checked((long)job.AcceptedSequence),
                        expected)
                    .Frame)
                .ToArray();
        }
        catch (Exception exception) when (exception is InvalidDataException
                                          or OverflowException)
        {
            throw DataLost(
                "Standalone Actor accepted queue is malformed.",
                exception);
        }
    }

    internal static bool IsExactSourceLeaseExpired(
        ZLinkOwnerLeaseReadResult lease,
        ZLinkActorRelocationSourceFence source) => lease switch
        {
            ZLinkOwnerLeaseReadResult.Missing => true,
            ZLinkOwnerLeaseReadResult.Found found =>
                source.OwnerLeaseGeneration > long.MaxValue
                || !StringComparer.Ordinal.Equals(
                    found.Token.OwnerId,
                    source.OwnerId)
                || found.Token.LeaseGeneration
                   != checked((long)source.OwnerLeaseGeneration)
                || found.LeaseExpiresAt <= found.StoreNow,
            _ => false
        };

    private static void ValidatePrepare(
        ZLinkServiceWireCodec.RelocationPrepareRecord prepare,
        RoutingId authenticatedSourceNodeRid)
    {
        if (prepare.Object.Kind != 1
            || prepare.RoundKind != 1
            || prepare.TargetAttemptGeneration == 0
            || prepare.SourceNodeRid != authenticatedSourceNodeRid
            || prepare.Coordinator.NodeRid != authenticatedSourceNodeRid
            || prepare.Participants.Count != 1
            || prepare.Participants[0].ParticipantId != 1)
            throw DataLost("Standalone Actor command 40 fence is invalid.");
    }

    private static ZLinkFrameworkException DataLost(
        string message,
        Exception? inner = null) => new(
        ZLinkFrameworkErrorKind.RelocationDataLost,
        message,
        isRetriable: false,
        inner);

    private readonly record struct AttemptKey(
        ulong RelocationHigh,
        ulong RelocationLow,
        ulong AttemptGeneration);

    private sealed class TargetStage(
        ZLinkServiceWireCodec.RelocationPrepareRecord prepare,
        RoutingId sourceNodeRid,
        ZLinkRelocationEnvelope envelope,
        ZLinkActorRuntimeState actorState,
        ZLinkActorAuthorityPayload targetAuthority,
        IReadOnlyList<ZLinkActorHandoffFrame> acceptedFrames)
    {
        internal ZLinkServiceWireCodec.RelocationPrepareRecord Prepare { get; } = prepare;
        internal RoutingId SourceNodeRid { get; } = sourceNodeRid;
        internal ZLinkRelocationEnvelope Envelope { get; } = envelope;
        internal ZLinkRelocationParticipantEnvelope Participant
            => Envelope.Participants[0];
        internal ZLinkActorRuntimeState ActorState { get; } = actorState;
        internal ZLinkActorAuthorityPayload TargetAuthority { get; } = targetAuthority;
        internal IReadOnlyList<ZLinkActorHandoffFrame> AcceptedFrames { get; } = acceptedFrames;
        internal DateTimeOffset CreatedAt { get; } = TimeProvider.System.GetUtcNow();
        internal bool AuthorityPublished { get; set; }

        internal void ValidateRetry(
            ZLinkServiceWireCodec.RelocationPrepareRecord retry,
            RoutingId authenticatedSourceNodeRid)
        {
            if (SourceNodeRid != authenticatedSourceNodeRid
                || !ZLinkServiceWireCodec.EncodeRelocationPrepare(Prepare)
                    .AsSpan()
                    .SequenceEqual(
                        ZLinkServiceWireCodec.EncodeRelocationPrepare(retry)))
                throw DataLost(
                    "Standalone Actor relocation retry changed its exact attempt.");
        }
    }
}

internal sealed partial class ZLinkFrameworkRuntime
{
    internal void RelayStandaloneActorRelocationTrailing(
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef sourceActorRef,
        IReadOnlyList<ZLinkActorHandoffFrame> frames)
    {
        if (frames.Count == 0) return;
        if (actorState.Handoff.RouteFrame(
                sourceActorRef,
                sourceActorRef,
                out var forwarding) != ZLinkActorFrameRoute.Forward
            || forwarding is null)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorLocationStale,
                $"Actor '{actorState.ActorId}' committed forwarding map is unavailable.");
        foreach (var frame in frames.OrderBy(static frame => frame.ArrivalIndex))
        {
            using var body = Message.From(frame.Body);
            ActorStragglerForwarder.Enqueue(
                forwarding.Value,
                frame.SourceNodeRid.Length == 0
                    ? default
                    : RoutingId.From(frame.SourceNodeRid),
                frame.SourceSessionRid.Length == 0
                    ? default
                    : RoutingId.From(frame.SourceSessionRid),
                frame.RequestId,
                frame.Flags,
                frame.RouteContext,
                ZLinkStreamProtocolDefaults.DecodeHeader(frame.Header),
                body);
        }
    }

    internal async ValueTask RestoreStandaloneActorRelocationSourceAsync(
        ZLinkActorRuntimeState actorState)
    {
        var frames = actorState.Handoff.AbortCapture();
        if (frames.Count == 0) return;
        var actorRef = actorState.NativeActorRef
                       ?? throw new ZLinkFrameworkException(
                           ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorState.ActorId}' source reference is unavailable during rollback.");
        var pipeline = new ZLinkActorInboundPipeline(
            this,
            new ZLinkEntrySpotActorInboundEndpoint(this));
        await pipeline.DispatchAsync(
                ZLinkActorHandoffFrames.Restore(actorRef, frames),
                CancellationToken.None)
            .ConfigureAwait(false);
    }

    internal async ValueTask CompleteStandaloneActorRelocationSourceAsync(
        IZLinkActor actor,
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef sourceActorRef,
        ZLinkBackendActorRef targetActorRef,
        CancellationToken cancellationToken)
    {
        await NotifyEntrySpotActorLeftAsync(
                actor,
                cancellationToken: cancellationToken)
            .ConfigureAwait(false);
        actorState.BindNativeActorRef(targetActorRef);
        actorState.InvalidateContext();
        await _actorSessionManager.FinalizeMigratedSourceAsync(
                actorState,
                sourceActorRef)
            .ConfigureAwait(false);
    }

    internal async ValueTask CompleteStandaloneActorRelocationTargetAsync(
        ZLinkActorRuntimeState actorState,
        ZLinkActorAuthorityPayload targetAuthority,
        string handoffId,
        CancellationToken cancellationToken)
    {
        var targetNode = GetSpotNodeRuntime(targetAuthority.NodeRid);
        var actor = actorState.Actor
                    ?? throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"Actor '{actorState.ActorId}' is not materialized on the relocation target.");
        await NotifyEntrySpotActorRelocatedAsync(
                actor,
                targetNode.Node.RoutingId,
                cancellationToken)
            .ConfigureAwait(false);
        await ReplayEntrySpotActorFramesAsync(
                actorState,
                actorState.Handoff.PrepareCanonicalMaintenanceReplay(handoffId),
                cancellationToken)
            .ConfigureAwait(false);
        while (true)
        {
            var frames = actorState.Handoff.SnapshotFinalReplay();
            if (frames.Count == 0) break;
            await ReplayEntrySpotActorFramesAsync(
                    actorState,
                    frames,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        var commit = await CommitCompletedSessionRouteAsync(
                actorState,
                handoffId,
                cancellationToken)
            .ConfigureAwait(false);
        var actorRef = actorState.NativeActorRef
                       ?? throw new ZLinkFrameworkException(
                           ZLinkFrameworkErrorKind.ActorRouteNotFound,
                           $"Actor '{actorState.ActorId}' target reference is unavailable.");
        var ownership = RequireActorRelocationLocationLifecycle(
                LocationLifecycle,
                actorState.ActorId)
            .ActorOwnership;
        var relocationId = Guid.ParseExact(handoffId, "N");
        var durable = await ownership.ReadTransferredActorAuthorityPhaseAsync(
                actorState.ActorId,
                actorRef.ToNative(targetAuthority.MeshName),
                cancellationToken)
            .ConfigureAwait(false);
        if (durable?.Phase.RelocationId != relocationId)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RelocationDataLost,
                $"Actor '{actorState.ActorId}' lost its committed relocation phase.");
        if (durable.Value.Phase.Phase
            == ZLinkActorRelocationAuthorityPhase.Completed)
            await ownership.AdvanceTransferredActorAuthorityPhaseAsync(
                    actorState.ActorId,
                    actorRef.ToNative(targetAuthority.MeshName),
                    relocationId,
                    ZLinkActorRelocationAuthorityPhase.Completed,
                    ZLinkActorRelocationAuthorityPhase.Steady,
                    cancellationToken)
                .ConfigureAwait(false);
        else if (durable.Value.Phase.Phase
                 != ZLinkActorRelocationAuthorityPhase.Steady)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RelocationDataLost,
                $"Actor '{actorState.ActorId}' target completion phase is not recoverable.");
        await ownership.NormalizeTransferredActorAuthorityAsync(
                actorState.ActorId,
                actorRef.ToNative(targetAuthority.MeshName),
                relocationId,
                cancellationToken)
            .ConfigureAwait(false);
        if (commit is not null)
        {
            actorState.CompleteRelocationSessionRoute(handoffId);
            await UnsealCompletedSessionRouteAsync(commit, cancellationToken)
                .ConfigureAwait(false);
        }
        actorState.Handoff.Complete(handoffId);
    }
}
