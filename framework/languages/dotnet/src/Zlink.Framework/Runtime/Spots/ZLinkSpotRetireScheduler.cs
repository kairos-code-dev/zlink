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

    ValueTask StageAsync(
        ZLinkSpotRetireReservation reservation,
        ZLinkPreparedAggregateRelocation relocation,
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
    IReadOnlyList<string> ActorIds);

internal sealed record ZLinkSpotRetireReservation(
    ZLinkSpotRetireInventory Inventory,
    ZLinkMeshNodeDescriptorKey TargetDescriptor,
    ulong TargetDescriptorLifecycleGeneration,
    ZLinkCapacityVector Capacity,
    ZLinkLocationOwnerToken TargetOwner);

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
    IZLinkAuthorityStore authorityStore,
    IZLinkRelocationStore relocationStore,
    IZLinkSpotRetireTarget target,
    ZLinkRelocationPermitPool permits)
{
    private const long SnapshotReservationBytes = 64L * 1024 * 1024;
    private const long EnvelopeHeaderBytes =
        sizeof(uint) + sizeof(ushort) + 16 + sizeof(ulong)
        + sizeof(int) + 32 + sizeof(int);

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
        var inventory = new ZLinkSpotRetireInventory(
            activation.MeshName,
            activation.NodeRid,
            activation.SourceNodeLifecycleGeneration,
            activation.SourceOwnerToken,
            activation.SpotId,
            activation.ResolveStableTypeForRetire(),
            activation.Spot.GetType(),
            instanceSpot,
            activation.ObjectGeneration,
            actorIds);
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
            ZLinkPreparedAggregateRelocation? prepared = null;
            ZLinkAggregateRelocationPublished? published = null;
            IReadOnlyList<ZLinkAcceptedWorkRecord> held = [];
            var committed = false;
            var targetPublished = false;
            var forwardingStarted = false;
            var sourceCommitted = false;
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
                if (!activation.FreezeRelocationIngress(
                        seal,
                        out held))
                    throw new InvalidOperationException(
                        $"SPOT '{activation.SpotId}' could not freeze accepted ingress before durable preparation.");
                var participants = await BuildParticipantsAsync(
                        activation,
                        seal,
                        application,
                        actorIds,
                        held,
                        aggregateId,
                        sealedSessionRoutes,
                        cancellationToken)
                    .ConfigureAwait(false);
                prepared = await new ZLinkAggregateRelocationCoordinator(
                        authorityStore,
                        relocationStore)
                    .PrepareAsync(
                        new ZLinkAggregateRelocationRequest(
                            aggregateId,
                            1,
                            participants,
                            reservation.TargetDescriptor,
                            reservation.TargetDescriptorLifecycleGeneration,
                            reservation.Capacity,
                            reservation.TargetOwner),
                        cancellationToken)
                    .ConfigureAwait(false);
                var actualPayloadBytes =
                    ZLinkRelocationEnvelopeCodec.MeasureEncodedLength(
                        prepared.Envelope);
                if (!permit.TryShrinkPayload(actualPayloadBytes))
                    throw new InvalidOperationException(
                        $"SPOT '{activation.SpotId}' relocation payload exceeded its sealed reservation.");

                await target.StageAsync(
                        reservation,
                        prepared,
                        cancellationToken)
                    .ConfigureAwait(false);
                published = await new ZLinkAggregateRelocationCoordinator(
                        authorityStore,
                        relocationStore)
                    .CommitAsync(prepared, cancellationToken)
                    .ConfigureAwait(false);
                committed = true;
                await CompleteCommittedAsync(cancellationToken)
                    .ConfigureAwait(false);
                return true;
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
                            prepared is not null
                                ? () => new ZLinkAggregateRelocationCoordinator(
                                        authorityStore,
                                        relocationStore)
                                    .AbortAsync(prepared!)
                                : null,
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
                                prepared?.Fence),
                            () =>
                            {
                                if (seal is not null
                                    && !activation.AbortRelocation(seal))
                                    throw new ZLinkRelocationDataLostException(
                                        $"SPOT '{activation.SpotId}' could not restore its source admission seal.");
                                return ValueTask.CompletedTask;
                            })
                        .ConfigureAwait(false);
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
                if (!forwardingStarted)
                {
                    var spotParticipant = published.Envelope.Participants.Single(
                        static participant => participant.ObjectKind
                            is ZLinkPlacementObjectKind.UserSpot
                            or ZLinkPlacementObjectKind.InstanceSpot);
                    activation.BindCommittedRelocationReplyRoutes(
                        spotParticipant.AcceptedJobs,
                        [],
                        reservation.TargetDescriptor.Rid,
                        checked(
                            spotParticipant.AuthorityOwnerGeneration + 1));
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
                        || !SameAcceptedWork(held, releasedHeld))
                        throw new ZLinkRelocationDataLostException(
                            $"SPOT '{activation.SpotId}' accepted ingress changed after its durable root was prepared.");
                    sourceCommitted = true;
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
                        [],
                        completionToken)
                    .ConfigureAwait(false);
                if (!sourceCompleted)
                {
                    await activation.InvokeRelocationClosingAfterCommitAsync(
                            deadline)
                        .ConfigureAwait(false);
                    await completeSource(activation, CancellationToken.None)
                        .ConfigureAwait(false);
                    sourceCompleted = true;
                }
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
            + SnapshotReservationBytes * snapshotParticipantCount);
    }

    private async ValueTask<ZLinkAggregateRelocationParticipant[]>
        BuildParticipantsAsync(
            ZLinkSpotActivation activation,
            ZLinkSpotRelocationSeal seal,
            ZLinkSpotRelocationApplicationState application,
            IReadOnlyList<string> actorIds,
        IReadOnlyList<ZLinkAcceptedWorkRecord> held,
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
        participants.Add(new ZLinkAggregateRelocationParticipant(
            new ZLinkRelocationParticipantEnvelope(
                spotKey,
                activation.Spot is IZLinkInstanceSpot
                    ? ZLinkPlacementObjectKind.InstanceSpot
                    : ZLinkPlacementObjectKind.UserSpot,
                spot.ObjectGeneration,
                spot.AuthorityOwnerGeneration,
                application.SpotState,
                seal.QueueSeal.QueueSeal.Captured.Select(
                        static record => new ZLinkRelocationQueuedJob(
                            record.AcceptedSequence,
                            record.Payload))
                    .Concat(held.Select(
                        static record => new ZLinkRelocationQueuedJob(
                            record.AcceptedSequence,
                            record.Payload)))
                    .OrderBy(static record => record.AcceptedSequence)
                    .ToArray(),
                seal.LogicalTimers),
            spot.StoreVersion,
            ZLinkAuthorityGenerationTransition.NewOwner,
            spot.Payload,
            ZLinkSpotRetireCompletionMarker.CreatePending()));

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
            participants.Add(new ZLinkAggregateRelocationParticipant(
                new ZLinkRelocationParticipantEnvelope(
                    key,
                    ZLinkPlacementObjectKind.Actor,
                    actor.ObjectGeneration,
                    actor.AuthorityOwnerGeneration,
                    application.ActorStates[actorId],
                    [],
                    []),
                actor.StoreVersion,
                ZLinkAuthorityGenerationTransition.NewOwner,
                relocationAuthority,
                ReadOnlyMemory<byte>.Empty));
        }
        return participants.ToArray();
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
