using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.Runtime.Spots;

/// <summary>
/// Target-side bridge for the service-wire relocation operation. The bridge
/// stages factory, application Restore, accepted journal and timers without
/// making the activation visible. It publishes only after the source commits
/// the aggregate authority fence.
/// </summary>
internal interface IZLinkSpotRetireTarget
{
    ValueTask<ZLinkSpotRetireReservation?> TryReserveAsync(
        ZLinkSpotRetireInventory inventory,
        CancellationToken cancellationToken);

    ValueTask<ZLinkSpotRetireReservation?> TryReserveForPreflightAsync(
        ZLinkSpotRetireInventory inventory,
        ZLinkRetirePreflightPlan plan,
        CancellationToken cancellationToken) =>
        TryReserveAsync(inventory, cancellationToken);

    ValueTask StageAsync(
        ZLinkSpotRetireReservation reservation,
        ZLinkPreparedSpotRetireStaging relocation,
        CancellationToken cancellationToken);

    ValueTask PublishAsync(
        ZLinkSpotRetireReservation reservation,
        ZLinkAggregateRelocationPublished relocation,
        CancellationToken cancellationToken);

    ValueTask AbortAsync(
        ZLinkSpotRetireReservation reservation,
        ZLinkAggregateFence? fence);

    ValueTask RelayCommittedAsync(
        ZLinkSpotRetireReservation reservation,
        ZLinkAggregateRelocationPublished relocation,
        IReadOnlyList<ZLinkAcceptedWorkRecord> held,
        CancellationToken cancellationToken);
}

internal sealed class ZLinkRetirePreflightPlan
{
    private readonly Dictionary<string, Usage> _usage = new(StringComparer.Ordinal);

    internal bool TryReserve(
        ZLinkMeshNodeDescriptor descriptor,
        ZLinkCapacityVector capacity,
        int activationCount = 1)
    {
        var key = $"{descriptor.MeshName}\0{descriptor.Rid.ToHex()}";
        _usage.TryGetValue(key, out var used);
        used ??= new Usage();
        if (!ZLinkSpotRetireTargetRuntime.HasHeadroom(
                descriptor.Capacity.Actors,
                checked(used.Actors + capacity.Actors))
            || !ZLinkSpotRetireTargetRuntime.HasHeadroom(
                descriptor.Capacity.Spots,
                checked(used.Spots + capacity.Spots))
            || descriptor.ActivationConcurrency.Limit
               - descriptor.ActivationConcurrency.Active
               < checked(used.Activations + activationCount))
            return false;

        if (capacity.SpotType is { } delta)
        {
            var typeCapacity = descriptor.Capacity.SpotTypes.SingleOrDefault(candidate =>
                candidate.ObjectKind == delta.ObjectKind
                && StringComparer.Ordinal.Equals(
                    candidate.StableType,
                    delta.StableType));
            used.SpotTypes.TryGetValue(
                (delta.ObjectKind, delta.StableType),
                out var usedForType);
            if (typeCapacity is null
                || !ZLinkSpotRetireTargetRuntime.HasHeadroom(
                    new ZLinkPopulationCapacity(
                        typeCapacity.Active,
                        typeCapacity.Reserved,
                        typeCapacity.Limit),
                    checked(usedForType + delta.Count)))
                return false;
        }

        used.Actors = checked(used.Actors + capacity.Actors);
        used.Spots = checked(used.Spots + capacity.Spots);
        used.Activations = checked(used.Activations + activationCount);
        if (capacity.SpotType is { } committedType)
            used.SpotTypes[(committedType.ObjectKind, committedType.StableType)] =
                checked(used.SpotTypes.GetValueOrDefault(
                    (committedType.ObjectKind, committedType.StableType))
                    + committedType.Count);
        _usage[key] = used;
        return true;
    }

    private sealed class Usage
    {
        internal int Actors { get; set; }
        internal int Spots { get; set; }
        internal int Activations { get; set; }
        internal Dictionary<(ZLinkPlacementObjectKind Kind, string StableType), int>
            SpotTypes { get; } = [];
    }
}

internal sealed record ZLinkSpotRetireInventory(
    string MeshName,
    RoutingId SourceNodeRid,
    ulong SourceNodeLifecycleGeneration,
    ZLinkLocationOwnerToken SourceOwner,
    string SpotId,
    string StableType,
    Type SpotType,
    bool InstanceSpot,
    ulong ObjectGeneration,
    IReadOnlyList<string> ActorIds,
    IReadOnlyList<ZLinkObjectCapability> RequiredCapabilities);

internal sealed record ZLinkSpotRetireReservation(
    ZLinkSpotRetireInventory Inventory,
    ZLinkMeshNodeDescriptorKey TargetDescriptor,
    ulong TargetDescriptorLifecycleGeneration,
    ZLinkCapacityVector Capacity,
    ZLinkLocationOwnerToken TargetOwner);

internal sealed record ZLinkPreparedSpotRetireStaging(
    ZLinkPreparedRelocation Root,
    IReadOnlyList<ZLinkAggregateRelocationParticipant> Participants)
{
    internal ZLinkRelocationStored Relocation => Root.Relocation;

    internal ZLinkRelocationEnvelope Envelope => Root.Envelope;
}

internal static class ZLinkSpotRetireCompletionMarker
{
    private static ReadOnlySpan<byte> SourceCleanupPending =>
        "zlink.spot.source.pending.v1"u8;

    private static ReadOnlySpan<byte> SourceCleanupCompleted =>
        "zlink.spot.source.completed.v1"u8;

    internal static byte[] CreatePending() =>
        SourceCleanupPending.ToArray();

    internal static byte[] CreateCompleted() =>
        SourceCleanupCompleted.ToArray();

    internal static bool IsCompleted(ReadOnlySpan<byte> payload) =>
        payload.SequenceEqual(SourceCleanupCompleted);

    internal static bool IsPending(ReadOnlySpan<byte> payload) =>
        payload.SequenceEqual(SourceCleanupPending);
}

/// <summary>
/// Runs one source Spot relocation transaction. Queue and timer dispatch stay
/// available until every process-wide permit is acquired. Any failure before
/// authority commit aborts target staging and resumes the exact source seal.
/// </summary>
internal sealed class ZLinkSpotRetireScheduler(
    IZLinkLocationStore authorityStore,
    IZLinkRelocationStore relocationStore,
    IZLinkSpotRetireTarget target,
    ZLinkRelocationPermitPool permits)
{
    private const long SnapshotReservationBytes = 64L * 1024 * 1024;
    private const long SourceIngressHoldReservationBytes =
        16L * 1024 * 1024;
    private const long EnvelopeHeaderBytes =
        sizeof(uint) + sizeof(ushort) + 16 + sizeof(ulong)
        + sizeof(int) + 32 + sizeof(int);

    internal async ValueTask<ZLinkFrameworkTerminationReason?> PreflightAsync(
        IReadOnlyList<(ZLinkSpotActivation Activation, bool Instance)> units,
        ZLinkRetirePreflightPlan plan,
        CancellationToken cancellationToken)
    {
        foreach (var unit in units)
        {
            var inventory = CreateInventory(unit.Activation, unit.Instance);
            if (inventory.RequiredCapabilities.Any(static capability =>
                    capability.Policy == ZLinkObjectMaintenancePolicyKind.Disabled))
                return ZLinkFrameworkTerminationReason.RelocationDisabled;
            if (await target.TryReserveForPreflightAsync(
                    inventory,
                    plan,
                    cancellationToken)
                    .ConfigureAwait(false) is null)
                return ZLinkFrameworkTerminationReason.TargetUnavailable;
        }
        return null;
    }

    private static ZLinkSpotRetireInventory CreateInventory(
        ZLinkSpotActivation activation,
        bool instanceSpot,
        IReadOnlyList<string>? actorIds = null)
    {
        actorIds ??= activation.SnapshotActorIds();
        return new ZLinkSpotRetireInventory(
            activation.MeshName,
            activation.NodeRid,
            activation.SourceNodeLifecycleGeneration,
            activation.SourceOwnerToken,
            activation.SpotId,
            activation.ResolveStableTypeForRetire(),
            activation.Spot.GetType(),
            instanceSpot,
            activation.ObjectGeneration,
            actorIds,
            activation.ResolveRetireCapabilities(instanceSpot));
    }

    internal async ValueTask<bool> TryRelocateAsync(
        ZLinkSpotActivation activation,
        bool instanceSpot,
        DateTimeOffset deadline,
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> completeSource,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(activation);
        ArgumentNullException.ThrowIfNull(completeSource);
        cancellationToken.ThrowIfCancellationRequested();

        var actorIds = activation.SnapshotActorIds();
        var participantKeys = new[]
            {
                ZLinkUserSpotAuthorityPayloadCodec.AuthorityKey(
                    activation.SpotId)
            }
            .Concat(actorIds.Select(
                ZLinkActorAuthorityPayloadCodec.AuthorityKey))
            .ToArray();
        var snapshotParticipantCount = CountSnapshotParticipants(
            activation,
            actorIds);
        var requiresCapture = snapshotParticipantCount > 0;
        if (!activation.IsRelocationReady)
            return false;
        var inventory = CreateInventory(activation, instanceSpot, actorIds);
        var reservation = await target.TryReserveAsync(
                inventory,
                cancellationToken)
            .ConfigureAwait(false);
        if (reservation is null)
            return false;

        ZLinkRelocationPermitPool.ZLinkRelocationPermitLease permit = default;
        if (!activation.TrySealRelocation(
                (captured, timers) =>
                    permits.TryAcquire(
                        ZLinkRelocationPermitRequest.Outbound(
                            CalculatePayloadReservation(
                                snapshotParticipantCount,
                                participantKeys,
                                captured,
                                timers),
                            requiresCapture,
                            allowOversizedPayload: !instanceSpot),
                        out permit),
                out var admittedSeal))
        {
            permit.Dispose();
            await target.AbortAsync(reservation, fence: null)
                .ConfigureAwait(false);
            return false;
        }

        using (permit)
        {
            ZLinkSpotRelocationSeal? seal = admittedSeal;
            ZLinkPreparedSpotRetireStaging? staging = null;
            ZLinkAggregateRelocationPublished? published = null;
            IReadOnlyList<ZLinkAcceptedWorkRecord> heldAtCutoff = [];
            IReadOnlyList<ZLinkAcceptedWorkRecord> committedHeld = [];
            var committed = false;
            var targetPublished = false;
            var forwardingStarted = false;
            var sourceCommitted = false;
            var committedHeldValidated = false;
            var sourceCompleted = false;
            var aggregateId = Guid.NewGuid();
            var sealedSessionRoutes =
                new Dictionary<string, ZLinkRemoteActorBoundSessionRoute>(
                    StringComparer.Ordinal);
            try
            {
                var sealedActorIds = activation.SnapshotActorIds();
                if (!sealedActorIds.SequenceEqual(
                        actorIds,
                        StringComparer.Ordinal))
                    throw new InvalidOperationException(
                        $"SPOT '{activation.SpotId}' participant inventory changed before the relocation seal.");
                var handoffId = aggregateId.ToString("N");
                foreach (var actorId in actorIds)
                {
                    var route = await activation
                        .SealActorBoundSessionRouteForRetireAsync(
                            actorId,
                            handoffId,
                            cancellationToken)
                        .ConfigureAwait(false);
                    if (route.IsBound)
                        sealedSessionRoutes.Add(actorId, route);
                }
                var application = await activation
                    .CaptureSealedRelocationApplicationStateAsync(
                        seal,
                        cancellationToken)
                    .ConfigureAwait(false);
                ValidateSnapshotPayloadSize(application.SpotState);
                foreach (var actorState in application.ActorStates.Values)
                    ValidateSnapshotPayloadSize(actorState);
                var participants = await BuildParticipantsAsync(
                        activation,
                        seal,
                        application,
                        actorIds,
                        aggregateId,
                        sealedSessionRoutes,
                        cancellationToken)
                    .ConfigureAwait(false);
                if (!activation.FreezeRelocationIngress(
                        seal,
                        out heldAtCutoff))
                    throw new InvalidOperationException(
                        "SPOT could not freeze its bounded ingress hold at the commit boundary.");
                ZLinkSpotRetireTargetRuntime.ValidateHeldRecords(
                    heldAtCutoff.Select(
                            static record => new ZLinkSpotRetireHeldRecord(
                                record.AcceptedSequence,
                                record.Payload.ToArray()))
                        .ToArray());
                participants = await AppendHeldIngressAsync(
                        activation.MeshName,
                        participants,
                        heldAtCutoff,
                        cancellationToken)
                    .ConfigureAwait(false);
                var sourceDescriptor = await ReadSourceDescriptorAsync(
                        activation.MeshName,
                        inventory.SourceNodeRid,
                        inventory.SourceNodeLifecycleGeneration,
                        inventory.SourceOwner,
                        cancellationToken)
                    .ConfigureAwait(false);
                var stagingEnvelope = ZLinkCanonicalSpotRelocationWriter.CreateInitial(
                    new ZLinkRelocationEnvelope(
                    aggregateId,
                    1,
                    ZLinkAggregateInventoryDigest.Compute(participants),
                    participants.Select(static participant =>
                            participant.Envelope)
                        .ToArray()),
                    activation.SpotId,
                    inventory.StableType,
                    inventory.SourceNodeRid,
                    sourceDescriptor.ApplicationVersion);
                var stagingRoot = await new ZLinkRelocationPublicationCoordinator(
                        authorityStore,
                        relocationStore)
                    .PrepareAsync(stagingEnvelope, cancellationToken)
                    .ConfigureAwait(false);
                staging = new ZLinkPreparedSpotRetireStaging(
                    stagingRoot,
                    participants);
                var actualPayloadBytes =
                    ZLinkRelocationEnvelopeCodec.MeasureEncodedLength(
                        staging.Envelope);
                if (!permit.TryShrinkPayload(checked(
                        actualPayloadBytes
                        + SourceIngressHoldReservationBytes)))
                    throw new InvalidOperationException(
                        $"SPOT '{activation.SpotId}' relocation payload exceeded its sealed reservation.");

                await target.StageAsync(
                        reservation,
                        staging,
                        cancellationToken)
                    .ConfigureAwait(false);
                published = new ZLinkAggregateRelocationPublished(
                    new ZLinkAggregateFence(aggregateId, 1),
                    staging.Relocation,
                    staging.Envelope);
                // StageAsync returns only after the source sends command 34
                // response=true. That response authorizes the target commit,
                // so cancellation or an unobserved commit must never reopen
                // source admission from this point forward.
                committed = true;
                await CompleteCommittedAsync(cancellationToken)
                    .ConfigureAwait(false);
                return true;
            }
            catch (ZLinkCanonicalRelocationDurablyAbortedException)
            {
                committed = false;
                await ExecutePrecommitAbortAsync(
                        null,
                        async () =>
                        {
                            var handoffId = aggregateId.ToString("N");
                            foreach (var (actorId, route) in sealedSessionRoutes)
                                await activation
                                    .AbortActorBoundSessionRouteSealForRetireAsync(
                                        actorId,
                                        route,
                                        handoffId,
                                        CancellationToken.None)
                                    .ConfigureAwait(false);
                        },
                        () => target.AbortAsync(
                            reservation,
                            new ZLinkAggregateFence(aggregateId, 1)),
                        () =>
                        {
                            if (seal is not null
                                && !activation.AbortRelocation(seal))
                                throw new ZLinkRelocationDataLostException(
                                    $"SPOT '{activation.SpotId}' could not restore its source admission seal.");
                            return ValueTask.CompletedTask;
                        })
                    .ConfigureAwait(false);
                await DiscardStagingAsync().ConfigureAwait(false);
                throw;
            }
            catch
            {
                if (committed)
                {
                    // Authority is the visibility boundary. Never resume the
                    // source after it points at the target; finish idempotent
                    // target publication and source cleanup instead.
                    await CompleteCommittedAsync(CancellationToken.None)
                        .ConfigureAwait(false);
                    return true;
                }
                else
                {
                    await ExecutePrecommitAbortAsync(
                            null,
                            async () =>
                            {
                                var handoffId = aggregateId.ToString("N");
                                foreach (var (actorId, route)
                                         in sealedSessionRoutes)
                                    await activation
                                        .AbortActorBoundSessionRouteSealForRetireAsync(
                                            actorId,
                                            route,
                                            handoffId,
                                            CancellationToken.None)
                                        .ConfigureAwait(false);
                            },
                            () => target.AbortAsync(
                                reservation,
                                new ZLinkAggregateFence(aggregateId, 1)),
                            () =>
                            {
                                if (seal is not null
                                    && !activation.AbortRelocation(seal))
                                    throw new ZLinkRelocationDataLostException(
                                        $"SPOT '{activation.SpotId}' could not restore its source admission seal.");
                                return ValueTask.CompletedTask;
                            })
                        .ConfigureAwait(false);
                    await DiscardStagingAsync().ConfigureAwait(false);
                }
                throw;
            }

            async ValueTask CompleteCommittedAsync(
                CancellationToken completionToken)
            {
                if (published is null || seal is null)
                    throw new InvalidOperationException(
                        "Committed SPOT relocation lost its publication state.");
                if (!targetPublished)
                {
                    await target.PublishAsync(
                            reservation,
                            published,
                            completionToken)
                        .ConfigureAwait(false);
                    targetPublished = true;
                }
                committed = true;
                if (!forwardingStarted)
                {
                    var spotParticipant = published.Envelope.Participants.Single(
                        static participant => participant.ObjectKind
                            is ZLinkPlacementObjectKind.UserSpot
                            or ZLinkPlacementObjectKind.InstanceSpot);
                    activation.BeginCommittedForwarding(
                        reservation.TargetDescriptor.Rid,
                        reservation.TargetDescriptorLifecycleGeneration,
                        spotParticipant.AuthorityOwnerGeneration,
                        checked(
                            spotParticipant.AuthorityOwnerGeneration + 1),
                        reservation.TargetOwner);
                    forwardingStarted = true;
                }
                if (!sourceCommitted)
                {
                    if (!activation.CommitRelocation(
                            seal,
                            out var releasedHeld)
                        || !SameAcceptedWork(
                            heldAtCutoff,
                            releasedHeld))
                        throw new ZLinkRelocationDataLostException(
                            $"SPOT '{activation.SpotId}' accepted ingress changed after its durable root was prepared.");
                    committedHeld = releasedHeld;
                    sourceCommitted = true;
                }
                if (!committedHeldValidated)
                {
                    ZLinkSpotRetireTargetRuntime.ValidateHeldRecords(
                        committedHeld.Select(
                                static record =>
                                    new ZLinkSpotRetireHeldRecord(
                                        record.AcceptedSequence,
                                        record.Payload.ToArray()))
                            .ToArray());
                    committedHeldValidated = true;
                }
                if (!sourceCompleted)
                {
                    await activation.InvokeRelocationClosingAfterCommitAsync(
                            deadline)
                        .ConfigureAwait(false);
                    await completeSource(activation, CancellationToken.None)
                        .ConfigureAwait(false);
                    sourceCompleted = true;
                }
                await new ZLinkAggregateRelocationCoordinator(
                        authorityStore,
                        relocationStore)
                    .CompleteSourceCleanupAsync(
                        published,
                        reservation.TargetDescriptor,
                        reservation.TargetDescriptorLifecycleGeneration,
                        reservation.TargetOwner,
                        completionToken)
                    .ConfigureAwait(false);
                await target.RelayCommittedAsync(
                        reservation,
                        published,
                        committedHeld,
                        completionToken)
                    .ConfigureAwait(false);
            }


            async ValueTask DiscardStagingAsync()
            {
                if (staging is null)
                    return;
                var discard = staging;
                staging = null;
                await new ZLinkRelocationPublicationCoordinator(
                        authorityStore,
                        relocationStore)
                    .DiscardPreparedAsync(discard.Root)
                    .ConfigureAwait(false);
            }
        }
    }

    internal static async ValueTask ExecutePrecommitAbortAsync(
        Func<ValueTask>? abortDurableAggregate,
        Func<ValueTask> restoreSessionRoutes,
        Func<ValueTask> cleanupTarget,
        Func<ValueTask> resumeSource)
    {
        ArgumentNullException.ThrowIfNull(restoreSessionRoutes);
        ArgumentNullException.ThrowIfNull(cleanupTarget);
        ArgumentNullException.ThrowIfNull(resumeSource);
        if (abortDurableAggregate is not null)
            await abortDurableAggregate().ConfigureAwait(false);
        await restoreSessionRoutes().ConfigureAwait(false);
        await cleanupTarget().ConfigureAwait(false);
        await resumeSource().ConfigureAwait(false);
    }

    private static int CountSnapshotParticipants(
        ZLinkSpotActivation activation,
        IReadOnlyList<string> actorIds)
    {
        var spot = activation.ResolveSpotRelocationRegistrationForRetire();
        if (spot.PolicyKind == 0)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestRejected,
                $"Relocation is disabled for SPOT '{activation.SpotId}'.");
        var count = spot.PolicyKind == 2 ? 1 : 0;
        foreach (var actorId in actorIds)
        {
            var actor =
                activation.ResolveActorRelocationRegistrationForRetire(actorId);
            if (actor.PolicyKind == 0)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RequestRejected,
                    $"Relocation is disabled for Actor '{actorId}'.");
            if (actor.PolicyKind == 2)
                count++;
        }
        return count;
    }

    internal static long CalculatePayloadReservation(
        int snapshotParticipantCount,
        IReadOnlyList<ZLinkAuthorityKey> participantKeys,
        IReadOnlyList<ZLinkAcceptedWorkRecord> captured,
        IReadOnlyList<ZLinkRelocationLogicalTimer> timers)
    {
        if (snapshotParticipantCount < 0)
            throw new ArgumentOutOfRangeException(
                nameof(snapshotParticipantCount));
        ArgumentNullException.ThrowIfNull(participantKeys);
        ArgumentNullException.ThrowIfNull(captured);
        ArgumentNullException.ThrowIfNull(timers);
        if (participantKeys.Count < 1)
            throw new ArgumentOutOfRangeException(nameof(participantKeys));

        // This is the exact encoded size of the framework-owned portion of
        // ZLinkRelocationEnvelopeCodec: header, participant manifest, accepted
        // journal and logical timers. Application state remains empty here;
        // Snapshot adapters reserve their documented maximum separately.
        long frameworkBytes = EnvelopeHeaderBytes;
        foreach (var key in participantKeys)
        {
            var keyBytes =
                System.Text.Encoding.UTF8.GetByteCount(key.Value);
            frameworkBytes = checked(
                frameworkBytes
                + sizeof(ushort) + keyBytes
                + sizeof(byte)
                + sizeof(ulong) + sizeof(ulong)
                + sizeof(int) // application state length
                + sizeof(int) // accepted job count
                + sizeof(int) // logical timer count
                + sizeof(int) // recovery payload length
                + sizeof(int)); // completion payload length
        }
        foreach (var record in captured)
            frameworkBytes = checked(
                frameworkBytes
                + sizeof(ulong)
                + sizeof(int)
                + record.Payload.Length);
        foreach (var timer in timers)
            frameworkBytes = checked(
                frameworkBytes
                + sizeof(ushort)
                + System.Text.Encoding.UTF8.GetByteCount(timer.TimerId)
                + sizeof(long)
                + sizeof(long)
                + sizeof(int)
                + timer.Payload.Length);
        return checked(
            frameworkBytes
            + ZLinkSpotRetireCompletionMarker.CreatePending().LongLength
            + SourceIngressHoldReservationBytes
            + SnapshotReservationBytes * snapshotParticipantCount);
    }

    internal static void ValidateSnapshotPayloadSize(
        ReadOnlyMemory<byte> payload)
    {
        if (payload.Length > SnapshotReservationBytes)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestRejected,
                "A relocation Snapshot payload cannot exceed 64 MiB.");
    }

    private async ValueTask<ZLinkAggregateRelocationParticipant[]>
        BuildParticipantsAsync(
            ZLinkSpotActivation activation,
            ZLinkSpotRelocationSeal seal,
            ZLinkSpotRelocationApplicationState application,
            IReadOnlyList<string> actorIds,
        Guid aggregateId,
        IReadOnlyDictionary<string, ZLinkRemoteActorBoundSessionRoute>
            sealedSessionRoutes,
        CancellationToken cancellationToken)
    {
        var participants = new List<ZLinkAggregateRelocationParticipant>(
            checked(actorIds.Count + 1));
        var spotKey = ZLinkUserSpotAuthorityPayloadCodec.AuthorityKey(
            activation.SpotId);
        var spot = await ReadOwnedAsync(spotKey, cancellationToken)
            .ConfigureAwait(false);
        var capturedJobs = await ResolveAcceptedJobsAsync(
                activation.MeshName,
                seal.QueueSeal.QueueSeal.Captured,
                cancellationToken)
            .ConfigureAwait(false);
        var spotKind = activation.Spot is IZLinkInstanceSpot
            ? ZLinkPlacementObjectKind.InstanceSpot
            : ZLinkPlacementObjectKind.UserSpot;
        var spotStableType = spotKind == ZLinkPlacementObjectKind.InstanceSpot
            ? ZLinkInstanceSpotAuthorityPayloadCodec.TryDecode(
                    spot.Payload.Span, out var instanceAuthority)
                ? instanceAuthority.StableType
                : throw new ZLinkRelocationDataLostException(
                    "The Instance SPOT authority payload is invalid.")
            : ZLinkUserSpotAuthorityPayloadCodec.TryDecode(
                    spot.Payload.Span, out var userAuthority)
                ? userAuthority.StableType
                : throw new ZLinkRelocationDataLostException(
                    "The User SPOT authority payload is invalid.");
        var spotRecovery = ZLinkCanonicalParticipantRecoveryCodec.Encode(
            new ZLinkCanonicalParticipantRecovery(
                spotKey,
                spotKind,
                spot.ObjectGeneration,
                spot.AuthorityOwnerGeneration,
                spot.StoreVersion,
                spotStableType,
                spot.Payload,
                ReadOnlyMemory<byte>.Empty));
        participants.Add(new ZLinkAggregateRelocationParticipant(
            new ZLinkRelocationParticipantEnvelope(
                spotKey,
                spotKind,
                spot.ObjectGeneration,
                spot.AuthorityOwnerGeneration,
                application.SpotState,
                capturedJobs,
                seal.LogicalTimers,
                RecoveryPayload: spotRecovery,
                CompletionPayload:
                    ZLinkSpotRetireCompletionMarker.CreatePending()),
            spot.StoreVersion,
            ZLinkAuthorityGenerationTransition.NewOwner,
            spot.Payload,
            ReadOnlyMemory<byte>.Empty));

        foreach (var actorId in actorIds)
        {
            var key = ZLinkActorAuthorityPayloadCodec.AuthorityKey(actorId);
            var actor = await ReadOwnedAsync(key, cancellationToken)
                .ConfigureAwait(false);
            var boundRoute = sealedSessionRoutes.TryGetValue(
                actorId,
                out var sealedRoute)
                ? sealedRoute
                : default;
            var relocationAuthority =
                ZLinkActorRelocationAuthorityPayloadCodec.Encode(
                    new ZLinkActorRelocationAuthorityPayload(
                        aggregateId,
                        ZLinkActorRelocationAuthorityPhase.Activated,
                        boundRoute,
                        actor.Payload));
            if (!ZLinkActorAuthorityPayloadCodec.TryDecodeRelocating(
                    actor.Payload.Span, out var actorAuthority))
                throw new ZLinkRelocationDataLostException(
                    $"Actor authority '{key.Value}' is invalid.");
            var actorRecovery = ZLinkCanonicalParticipantRecoveryCodec.Encode(
                new ZLinkCanonicalParticipantRecovery(
                    key,
                    ZLinkPlacementObjectKind.Actor,
                    actor.ObjectGeneration,
                    actor.AuthorityOwnerGeneration,
                    actor.StoreVersion,
                    actorAuthority.StableType,
                    relocationAuthority,
                    ReadOnlyMemory<byte>.Empty));
            participants.Add(new ZLinkAggregateRelocationParticipant(
                new ZLinkRelocationParticipantEnvelope(
                    key,
                    ZLinkPlacementObjectKind.Actor,
                    actor.ObjectGeneration,
                    actor.AuthorityOwnerGeneration,
                    application.ActorStates[actorId],
                    [],
                    [],
                    RecoveryPayload: actorRecovery),
                actor.StoreVersion,
                ZLinkAuthorityGenerationTransition.NewOwner,
                relocationAuthority,
                ReadOnlyMemory<byte>.Empty));
        }
        return participants.ToArray();
    }

    private async ValueTask<ZLinkAggregateRelocationParticipant[]>
        AppendHeldIngressAsync(
        string meshName,
        IReadOnlyList<ZLinkAggregateRelocationParticipant> participants,
        IReadOnlyList<ZLinkAcceptedWorkRecord> held,
        CancellationToken cancellationToken)
    {
        if (held.Count == 0)
            return participants.ToArray();
        var spot = participants.Single(static participant =>
            participant.Envelope.ObjectKind
                is ZLinkPlacementObjectKind.UserSpot
                or ZLinkPlacementObjectKind.InstanceSpot);
        var heldJobs = await ResolveAcceptedJobsAsync(
                meshName,
                held,
                cancellationToken)
            .ConfigureAwait(false);
        var accepted = spot.Envelope.AcceptedJobs
            .Concat(heldJobs)
            .OrderBy(static record => record.AcceptedSequence)
            .ToArray();
        for (var index = 1; index < accepted.Length; index++)
            if (accepted[index - 1].AcceptedSequence
                >= accepted[index].AcceptedSequence)
                throw new ZLinkRelocationDataLostException(
                    "SPOT relocation accepted journal contains duplicate or out-of-order sequence values.");
        return participants.Select(participant =>
                participant == spot
                    ? participant with
                    {
                        Envelope = participant.Envelope with
                        {
                            AcceptedJobs = accepted
                        }
                    }
                    : participant)
            .ToArray();
    }

    private ValueTask<IReadOnlyList<ZLinkRelocationQueuedJob>>
        ResolveAcceptedJobsAsync(
            string meshName,
            IReadOnlyList<ZLinkAcceptedWorkRecord> records,
            CancellationToken cancellationToken)
    {
        _ = meshName;
        cancellationToken.ThrowIfCancellationRequested();
        if (records.Count == 0)
            return ValueTask.FromResult<IReadOnlyList<ZLinkRelocationQueuedJob>>(
                []);
        return ValueTask.FromResult(ResolveFrozenAcceptedJobs(records));
    }

    internal static IReadOnlyList<ZLinkRelocationQueuedJob>
        ResolveFrozenAcceptedJobs(
            IReadOnlyList<ZLinkAcceptedWorkRecord> records)
    {
        return records.Select(record =>
        {
            var journal = ZLinkSpotAcceptedJournal.Decode(record.Payload.Span);
            if (journal.RequestSource is not { } source)
                throw new ZLinkRelocationDataLostException(
                    "Accepted request source fence was not frozen at ingress.");
            return new ZLinkRelocationQueuedJob(
                record.AcceptedSequence,
                record.Payload)
            {
                RequestSource = new ZLinkCanonicalRequestSourceFence(
                    source.OwnerId,
                    source.LeaseGeneration,
                    source.NodeRid.ToHex(),
                    source.NodeGeneration)
            };
        }).OrderBy(static job => job.AcceptedSequence).ToArray();
    }

    private async ValueTask<ZLinkMeshNodeDescriptor> ReadSourceDescriptorAsync(
        string meshName,
        RoutingId sourceNodeRid,
        ulong sourceNodeGeneration,
        ZLinkLocationOwnerToken sourceOwner,
        CancellationToken cancellationToken)
    {
        var descriptors = await authorityStore.ListAllMeshNodesAsync(
                meshName,
                cancellationToken)
            .ConfigureAwait(false);
        return descriptors.SingleOrDefault(descriptor =>
                   descriptor.Rid == sourceNodeRid
                   && descriptor.LifecycleGeneration == sourceNodeGeneration
                   && descriptor.OwnerId == sourceOwner.OwnerId
                   && descriptor.LeaseGeneration == sourceOwner.LeaseGeneration)
               ?? throw new ZLinkRelocationDataLostException(
                   "Relocation source descriptor fence changed before canonical capture.");
    }

    private static bool SameAcceptedWork(
        IReadOnlyList<ZLinkAcceptedWorkRecord> expected,
        IReadOnlyList<ZLinkAcceptedWorkRecord> actual)
    {
        if (expected.Count != actual.Count)
            return false;
        for (var index = 0; index < expected.Count; index++)
            if (expected[index].AcceptedSequence
                    != actual[index].AcceptedSequence
                || !expected[index].Payload.Span.SequenceEqual(
                    actual[index].Payload.Span))
                return false;
        return true;
    }

    private async ValueTask<ZLinkAuthoritySnapshot> ReadOwnedAsync(
        ZLinkAuthorityKey key,
        CancellationToken cancellationToken)
    {
        var read = await authorityStore.ReadAuthorityAsync(key, cancellationToken)
            .ConfigureAwait(false);
        return read is ZLinkAuthorityReadResult.Found found
            ? found.Snapshot
            : throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Relocation authority '{key.Value}' is not Ready.");
    }
}
