using Zlink.Framework.Runtime.Actors;

namespace Zlink.Framework.Runtime.Locations;

internal readonly record struct ZLinkCommittedActorAuthority(
    ulong AuthorityOwnerGeneration,
    string MeshName,
    ulong NodeGeneration,
    ulong OwnerLeaseGeneration);

internal enum ZLinkActorClaimStatus
{
    Claimed = 1,
    AlreadyOwned = 2,
    Conflict = 3,
    StoreFailure = 4
}

internal readonly record struct ZLinkActorClaimResult(
    ZLinkActorClaimStatus Status,
    ZLinkActorLocation? Existing);

internal readonly record struct ZLinkActorClaimActivation<TActor>(
    TActor? Activated,
    ZLinkActorLocation? ExistingLocation)
    where TActor : class;

internal sealed class ZLinkActorOwnershipCoordinator(
    ZLinkLocationRuntime runtime,
    ZLinkStoreLocationResolvers resolver) : IZLinkActorLocationLifecycle, IAsyncDisposable
{
    private readonly object _gate = new();
    private readonly object _disposeStartGate = new();
    private readonly SemaphoreSlim _backgroundDrainGate = new(1, 1);
    private readonly Dictionary<string, TrackedActor> _actors = new(StringComparer.Ordinal);
    private readonly HashSet<TrackedActor> _trackedActors = [];
    private CancellationTokenSource _reconciliationStop = new();
    private bool _backgroundStopping;
    private int _disposed;
    private Task? _disposeTask;

    public async ValueTask<ZLinkActorClaimActivation<TActor>> ExecuteActorClaimThenActivateAsync<TActor>(
        string meshName,
        string actorType,
        string actorId,
        RoutingId nodeRid,
        Func<CancellationToken, ValueTask>? deactivate,
        Func<CancellationToken, ValueTask<TActor>> activate,
        CancellationToken cancellationToken,
        ZLinkActorClaimMode claimMode = ZLinkActorClaimMode.NewOwner)
        where TActor : class
    {
        if (claimMode == ZLinkActorClaimMode.StagedRelocation)
            return new ZLinkActorClaimActivation<TActor>(
                await activate(cancellationToken).ConfigureAwait(false),
                null);

        var claim = await ClaimActorCoreAsync(
                meshName, actorType, actorId, nodeRid, deactivate, claimMode, cancellationToken)
            .ConfigureAwait(false);
        switch (claim.Status)
        {
            case ZLinkActorClaimStatus.AlreadyOwned:
                return new ZLinkActorClaimActivation<TActor>(null, null);

            case ZLinkActorClaimStatus.Conflict:
                return new ZLinkActorClaimActivation<TActor>(null, claim.Existing);

            case ZLinkActorClaimStatus.StoreFailure:
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorCreateFailed,
                    $"Actor '{actorId}' cannot be created because the location store is unavailable.");
        }

        try
        {
            var activated = await activate(cancellationToken).ConfigureAwait(false);
            return new ZLinkActorClaimActivation<TActor>(activated, null);
        }
        catch (Exception activationFailure)
        {
            // The claim preceded the failed activation; without a rollback
            // the key would stay owned by an instance that never existed.
            MarkActivationFailed(actorId);
            try
            {
                await ReleaseActorAsync(actorId, CancellationToken.None).ConfigureAwait(false);
            }
            catch (Exception releaseFailure)
            {
                StartActivationFailureReconciliation(actorId);
                throw new AggregateException(activationFailure, releaseFailure);
            }
            throw;
        }
    }

    private async ValueTask<ZLinkActorClaimResult> ClaimActorCoreAsync(
        string meshName,
        string actorType,
        string actorId,
        RoutingId nodeRid,
        Func<CancellationToken, ValueTask>? deactivate,
        ZLinkActorClaimMode claimMode,
        CancellationToken cancellationToken)
    {
        var canonical = actorId;
        while (true)
        {
            var retryFailedActivation = false;
            lock (_gate)
            {
                if (_actors.TryGetValue(canonical, out var tracked))
                {
                    if (!tracked.ActivationFailed)
                        return new ZLinkActorClaimResult(ZLinkActorClaimStatus.AlreadyOwned, null);
                    retryFailedActivation = true;
                }
            }

            if (!retryFailedActivation) break;
            try
            {
                await ReleaseActorAsync(actorId, cancellationToken).ConfigureAwait(false);
            }
            catch
            {
                return new ZLinkActorClaimResult(ZLinkActorClaimStatus.StoreFailure, null);
            }
        }

        ZLinkAuthorityReadResult read;
        try
        {
            read = await runtime.Store.ReadAuthorityAsync(
                    ZLinkActorAuthorityPayloadCodec.AuthorityKey(actorId),
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch
        {
            return new ZLinkActorClaimResult(ZLinkActorClaimStatus.StoreFailure, null);
        }

        if (read is not ZLinkAuthorityReadResult.Found found
            || found.Snapshot.Allocation.ObjectKind != ZLinkPlacementObjectKind.Actor
            || !ZLinkActorAuthorityPayloadCodec.TryDecode(
                found.Snapshot.Payload.Span,
                out var authority))
            return read is ZLinkAuthorityReadResult.Missing
                ? new ZLinkActorClaimResult(ZLinkActorClaimStatus.StoreFailure, null)
                : new ZLinkActorClaimResult(ZLinkActorClaimStatus.Conflict, null);

        if (!string.Equals(authority.ActorId, actorId, StringComparison.Ordinal)
            || !string.Equals(authority.StableType, actorType, StringComparison.Ordinal)
            || !string.Equals(authority.MeshName, meshName, StringComparison.Ordinal)
            || authority.NodeRid != nodeRid)
            return new ZLinkActorClaimResult(
                ZLinkActorClaimStatus.Conflict,
                ToLocation(found.Snapshot, authority));

        lock (_gate)
        {
            var tracked = new TrackedActor(found.Snapshot, authority, deactivate);
            _actors[canonical] = tracked;
            _trackedActors.Add(tracked);
        }
        return new ZLinkActorClaimResult(ZLinkActorClaimStatus.Claimed, null);
    }

    public async ValueTask PublishActorRefAsync(
        string actorId,
        ActorRef actorRef,
        CancellationToken cancellationToken = default)
    {
        await RenewOwnedActorAsync(
                actorId,
                static payload => payload,
                actorRef,
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask NotifyActorJoinedSpotAsync(
        string actorId,
        string spotId,
        ulong spotGeneration,
        CancellationToken cancellationToken = default)
    {
        await RenewOwnedActorAsync(
                actorId,
                payload => payload with
                {
                    CurrentSpotKind = ZLinkSpotKind.User,
                    CurrentSpotId = spotId,
                    CurrentSpotGeneration = spotGeneration
                },
                expectedActorRef: null,
                cancellationToken: cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask AdvanceTransferredActorAuthorityPhaseAsync(
        string actorId,
        ActorRef actorRef,
        Guid relocationId,
        ZLinkActorRelocationAuthorityPhase expected,
        ZLinkActorRelocationAuthorityPhase next,
        CancellationToken cancellationToken = default)
    {
        if ((byte)next != (byte)expected + 1)
            throw new ArgumentOutOfRangeException(
                nameof(next),
                "Actor relocation authority phases must advance one step.");
        var key = ZLinkActorAuthorityPayloadCodec.AuthorityKey(actorId);
        while (true)
        {
            var read = await runtime.Store.ReadAuthorityAsync(key, cancellationToken)
                .ConfigureAwait(false);
            if (read is not ZLinkAuthorityReadResult.Found found
                || found.Snapshot.ObjectGeneration != actorRef.ObjectGeneration
                || !TryDecodeRelocationPhase(
                    found.Snapshot.Payload,
                    out var phase,
                    out var publication)
                || phase.RelocationId != relocationId)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorLocationStale,
                    $"Actor '{actorId}' relocation authority fence changed.",
                    true);
            if (phase.Phase == next)
                return;
            if (phase.Phase != expected)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorMoving,
                    $"Actor '{actorId}' relocation phase cannot advance from '{phase.Phase}' to '{next}'.");

            var nextPhase = phase with { Phase = next };
            var nextPayload = EncodeRelocationPhase(nextPhase, publication);
            var result = await runtime.Store.CompareExchangeAuthorityAsync(
                    key,
                    found.Snapshot.StoreVersion,
                    new ZLinkAuthorityMutation.Put(
                        nextPayload,
                        ZLinkAuthorityGenerationTransition.Preserve,
                        null,
                        null),
                    cancellationToken)
                .ConfigureAwait(false);
            if (result is ZLinkAuthorityCompareExchangeResult.Stored stored)
            {
                UpdateTrackedSnapshot(actorId, stored.Snapshot);
                return;
            }
            if (result is not ZLinkAuthorityCompareExchangeResult.Conflict)
                throw new InvalidOperationException(
                    "Authority Store rejected an Actor relocation phase transition.");
        }
    }

    internal async ValueTask NormalizeTransferredActorAuthorityAsync(
        string actorId,
        ActorRef actorRef,
        Guid relocationId,
        CancellationToken cancellationToken = default)
    {
        var key = ZLinkActorAuthorityPayloadCodec.AuthorityKey(actorId);
        while (true)
        {
            var read = await runtime.Store.ReadAuthorityAsync(key, cancellationToken)
                .ConfigureAwait(false);
            if (read is not ZLinkAuthorityReadResult.Found found
                || found.Snapshot.ObjectGeneration != actorRef.ObjectGeneration)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorLocationStale,
                    $"Actor '{actorId}' relocation authority disappeared before normalization.",
                    true);
            if (!TryDecodeRelocationPhase(
                    found.Snapshot.Payload,
                    out var phase,
                    out var publication))
            {
                if (ZLinkActorAuthorityPayloadCodec.TryDecode(
                        found.Snapshot.Payload.Span,
                        out _))
                    return;
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorLocationStale,
                    $"Actor '{actorId}' authority is not a completed relocation.");
            }
            if (phase.RelocationId != relocationId
                || phase.Phase != ZLinkActorRelocationAuthorityPhase.Steady)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorMoving,
                    $"Actor '{actorId}' relocation is not steady.");

            var normalized = publication is null
                ? phase.ApplicationPayload
                : ZLinkRelocationAuthorityPayloadCodec.Encode(
                    publication with
                    {
                        ApplicationPayload = phase.ApplicationPayload
                    });
            var result = await runtime.Store.CompareExchangeAuthorityAsync(
                    key,
                    found.Snapshot.StoreVersion,
                    new ZLinkAuthorityMutation.Put(
                        normalized,
                        ZLinkAuthorityGenerationTransition.Preserve,
                        null,
                        null),
                    cancellationToken)
                .ConfigureAwait(false);
            if (result is ZLinkAuthorityCompareExchangeResult.Stored stored)
            {
                UpdateTrackedSnapshot(actorId, stored.Snapshot);
                return;
            }
            if (result is not ZLinkAuthorityCompareExchangeResult.Conflict)
                throw new InvalidOperationException(
                    "Authority Store rejected Actor steady normalization.");
        }
    }

    internal async ValueTask<(
        ZLinkAuthoritySnapshot Authority,
        ZLinkActorRelocationAuthorityPayload Phase)?>
        ReadTransferredActorAuthorityPhaseAsync(
            string actorId,
            ActorRef actorRef,
            CancellationToken cancellationToken = default)
    {
        var read = await runtime.Store.ReadAuthorityAsync(
                ZLinkActorAuthorityPayloadCodec.AuthorityKey(actorId),
                cancellationToken)
            .ConfigureAwait(false);
        return read is ZLinkAuthorityReadResult.Found found
               && found.Snapshot.ObjectGeneration == actorRef.ObjectGeneration
               && TryDecodeRelocationPhase(
                   found.Snapshot.Payload,
                   out var phase,
                   out _)
            ? (found.Snapshot, phase)
            : null;
    }

    internal async ValueTask<ZLinkCommittedActorAuthority>
        CommitTransferredActorAuthorityAsync(
        string actorId,
        ActorRef actorRef,
        string meshName,
        string spotId,
        ulong spotGeneration,
        ZLinkSpotKind spotKind,
        Guid relocationId,
        ZLinkRemoteActorBoundSessionRoute boundSessionRoute,
        ZLinkRelocationManifestReference relocationReference,
        Func<CancellationToken, ValueTask>? deactivate,
        CancellationToken cancellationToken = default)
    {
        var key = ZLinkActorAuthorityPayloadCodec.AuthorityKey(actorId);
        var read = await runtime.Store.ReadAuthorityAsync(key, cancellationToken)
            .ConfigureAwait(false);
        if (read is not ZLinkAuthorityReadResult.Found found
            || !ZLinkActorAuthorityPayloadCodec.TryDecodeRelocating(
                found.Snapshot.Payload.Span,
                out var authority))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorId}' does not have readable authority during handoff.");

        var snapshot = found.Snapshot;
        if (snapshot.ObjectGeneration != actorRef.ObjectGeneration)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorGenerationStale,
                $"Actor '{actorId}' authority generation changed during handoff.");
        if (authority.NodeRid == actorRef.NodeRid
            && string.Equals(authority.CurrentSpotId, spotId, StringComparison.Ordinal)
            && authority.CurrentSpotGeneration == spotGeneration
            && authority.CurrentSpotKind == spotKind)
        {
            if (!ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                    snapshot.Payload.Span,
                    out var existingPublication)
                || !string.Equals(
                    existingPublication.Reference,
                    relocationReference.Reference,
                    StringComparison.Ordinal)
                || existingPublication.ChecksumCrc32c
                != relocationReference.ChecksumCrc32c)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RelocationDataLost,
                    $"Actor '{actorId}' committed authority references another relocation root.",
                    isRetriable: false);
            TrackCommittedActorAuthority(actorId, snapshot, authority, deactivate);
            return new ZLinkCommittedActorAuthority(
                snapshot.AuthorityOwnerGeneration,
                authority.MeshName,
                authority.NodeGeneration,
                authority.OwnerLeaseGeneration);
        }

        var descriptors = await resolver.ListLiveMeshNodesAsync(
                meshName,
                cancellationToken)
            .ConfigureAwait(false);
        var target = descriptors.SingleOrDefault(descriptor =>
            descriptor.Rid == actorRef.NodeRid);
        if (target is null
            || target.State == ZLinkFrameworkRuntimeState.Draining
            || target.LifecycleGeneration == 0
            || string.IsNullOrWhiteSpace(target.OwnerId)
            || target.LeaseGeneration <= 0)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorId}' handoff target is not a live mesh node.");

        var targetOwner = new ZLinkLocationOwnerToken(
            target.OwnerId,
            target.LeaseGeneration);
        var sourceOwner = new ZLinkLocationOwnerToken(
            snapshot.OwnerId,
            snapshot.OwnerLeaseGeneration);
        var reserve = await runtime.Store.ReserveRelocationCapacityAsync(
                new ZLinkRelocationCapacityReservationRequest(
                    relocationId,
                    key,
                    snapshot.StoreVersion,
                    ZLinkPlacementObjectKind.Actor,
                    snapshot.Allocation.StableType,
                    snapshot.Allocation.Descriptor,
                    snapshot.Allocation.DescriptorLifecycleGeneration,
                    sourceOwner,
                    new ZLinkMeshNodeDescriptorKey(meshName, target.Rid),
                    target.LifecycleGeneration,
                    targetOwner,
                    new ZLinkCapacityVector(1, 0, null)),
                cancellationToken)
            .ConfigureAwait(false);
        if (reserve is ZLinkRelocationCapacityReserveResult.Conflict reserveConflict)
        {
            if (TryResolveCommittedAuthority(
                    reserveConflict.Current,
                    actorRef,
                    spotId,
                    spotGeneration,
                    spotKind,
                    relocationId,
                    relocationReference,
                    out var currentSnapshot,
                    out var currentAuthority))
            {
                TrackCommittedActorAuthority(
                    actorId,
                    currentSnapshot,
                    currentAuthority,
                    deactivate);
                return new ZLinkCommittedActorAuthority(
                    currentSnapshot.AuthorityOwnerGeneration,
                    currentAuthority.MeshName,
                    currentAuthority.NodeGeneration,
                    currentAuthority.OwnerLeaseGeneration);
            }
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorLocationStale,
                $"Actor '{actorId}' authority changed during handoff.",
                true);
        }
        if (reserve is ZLinkRelocationCapacityReserveResult.PlacementCapacityExhausted)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.PlacementCapacityExhausted,
                $"Actor '{actorId}' handoff target has no Actor capacity.",
                true);
        if (reserve is ZLinkRelocationCapacityReserveResult.TargetUnavailable)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorRouteNotFound,
                $"Actor '{actorId}' handoff target became unavailable.",
                true);
        var capacityFence = reserve switch
        {
            ZLinkRelocationCapacityReserveResult.Reserved reserved => reserved.Fence,
            ZLinkRelocationCapacityReserveResult.AlreadyReserved existing => existing.Fence,
            _ => throw new InvalidOperationException(
                "The authority store returned an invalid relocation capacity result.")
        };

        var applicationPayload = ZLinkActorAuthorityPayloadCodec.Encode(
            authority with
            {
                State = ZLinkActorAuthorityState.Ready,
                CurrentSpotId = spotId,
                CurrentSpotGeneration = spotGeneration,
                CurrentSpotKind = spotKind,
                OwnerId = targetOwner.OwnerId,
                OwnerLeaseGeneration = checked((ulong)targetOwner.LeaseGeneration),
                MeshName = meshName,
                NodeRid = target.Rid,
                NodeGeneration = target.LifecycleGeneration
            });
        var phasePayload = ZLinkActorRelocationAuthorityPayloadCodec.Encode(
            new ZLinkActorRelocationAuthorityPayload(
                relocationId,
                ZLinkActorRelocationAuthorityPhase.Activated,
                boundSessionRoute,
                applicationPayload));
        var authorityPayload = ZLinkRelocationAuthorityPayloadCodec.Encode(
            new ZLinkRelocationAuthorityPayload(
                relocationReference.Reference,
                relocationReference.ChecksumCrc32c,
                relocationReference.AggregateId,
                relocationReference.AggregateGeneration,
                relocationReference.InventoryDigest,
                targetOwner.OwnerId,
                targetOwner.LeaseGeneration,
                phasePayload));

        var committed = false;
        try
        {
            var result = await runtime.Store.CompareExchangeAuthorityAsync(
                    key,
                    snapshot.StoreVersion,
                    new ZLinkAuthorityMutation.Put(
                        authorityPayload,
                        ZLinkAuthorityGenerationTransition.NewOwner,
                        targetOwner,
                        capacityFence),
                    cancellationToken)
                .ConfigureAwait(false);
            switch (result)
            {
                case ZLinkAuthorityCompareExchangeResult.Stored stored:
                    committed = true;
                    TrackCommittedActorAuthority(
                        actorId,
                        stored.Snapshot,
                        authority with
                        {
                            State = ZLinkActorAuthorityState.Ready,
                            CurrentSpotId = spotId,
                            CurrentSpotGeneration = spotGeneration,
                            CurrentSpotKind = spotKind,
                            OwnerId = targetOwner.OwnerId,
                            OwnerLeaseGeneration = checked((ulong)targetOwner.LeaseGeneration),
                            MeshName = meshName,
                            NodeRid = target.Rid,
                            NodeGeneration = target.LifecycleGeneration
                        },
                        deactivate);
                    return new ZLinkCommittedActorAuthority(
                        stored.Snapshot.AuthorityOwnerGeneration,
                        meshName,
                        target.LifecycleGeneration,
                        checked((ulong)targetOwner.LeaseGeneration));

                case ZLinkAuthorityCompareExchangeResult.Conflict conflict:
                    if (TryResolveCommittedAuthority(
                            conflict.Current,
                            actorRef,
                            spotId,
                            spotGeneration,
                            spotKind,
                            relocationId,
                            relocationReference,
                            out var currentSnapshot,
                            out var currentAuthority))
                    {
                        committed = true;
                        TrackCommittedActorAuthority(
                            actorId,
                            currentSnapshot,
                            currentAuthority,
                            deactivate);
                        return new ZLinkCommittedActorAuthority(
                            currentSnapshot.AuthorityOwnerGeneration,
                            currentAuthority.MeshName,
                            currentAuthority.NodeGeneration,
                            currentAuthority.OwnerLeaseGeneration);
                    }
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorLocationStale,
                        $"Actor '{actorId}' authority changed during handoff.",
                        true);

                case ZLinkAuthorityCompareExchangeResult.GenerationExhausted:
                    throw new ZLinkAuthorityGenerationExhaustedException(
                        $"committing Actor '{actorId}' handoff authority");

                default:
                    throw new InvalidOperationException(
                        "The authority store returned an invalid Actor handoff result.");
            }
        }
        finally
        {
            if (!committed)
                await runtime.Store.AbortRelocationCapacityAsync(
                        capacityFence,
                        CancellationToken.None)
                    .ConfigureAwait(false);
        }
    }

    private static bool TryResolveCommittedAuthority(
        ZLinkAuthorityReadResult current,
        ActorRef actorRef,
        string spotId,
        ulong spotGeneration,
        ZLinkSpotKind spotKind,
        Guid relocationId,
        ZLinkRelocationManifestReference relocationReference,
        out ZLinkAuthoritySnapshot snapshot,
        out ZLinkActorAuthorityPayload authority)
    {
        if (current is ZLinkAuthorityReadResult.Found found
            && found.Snapshot.ObjectGeneration == actorRef.ObjectGeneration
            && TryDecodeRelocationPhase(
                    found.Snapshot.Payload,
                    out var phase,
                    out var publication)
            && phase.RelocationId == relocationId
            && publication is not null
            && string.Equals(
                publication.Reference,
                relocationReference.Reference,
                StringComparison.Ordinal)
            && publication.ChecksumCrc32c
            == relocationReference.ChecksumCrc32c
            && publication.AggregateId
            == relocationReference.AggregateId
            && ZLinkActorAuthorityPayloadCodec.TryDecodeRelocating(
                found.Snapshot.Payload.Span,
                out authority)
            && authority.NodeRid == actorRef.NodeRid
            && string.Equals(authority.CurrentSpotId, spotId, StringComparison.Ordinal)
            && authority.CurrentSpotGeneration == spotGeneration
            && authority.CurrentSpotKind == spotKind)
        {
            snapshot = found.Snapshot;
            return true;
        }

        snapshot = null!;
        authority = null!;
        return false;
    }

    private void TrackCommittedActorAuthority(
        string actorId,
        ZLinkAuthoritySnapshot snapshot,
        ZLinkActorAuthorityPayload payload,
        Func<CancellationToken, ValueTask>? deactivate)
    {
        lock (_gate)
        {
            if (_actors.TryGetValue(actorId, out var current))
            {
                current.Snapshot = snapshot;
                current.Payload = payload;
                return;
            }

            var tracked = new TrackedActor(snapshot, payload, deactivate);
            _actors.Add(actorId, tracked);
            _trackedActors.Add(tracked);
        }
    }

    internal async ValueTask NotifyActorMovedToEntrySpotAsync(
        string actorId,
        RoutingId targetNodeRid,
        CancellationToken cancellationToken = default)
    {
        await RenewOwnedActorAsync(
                actorId,
                payload => payload.NodeRid != targetNodeRid
                    ? throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorLocationStale,
                        $"Actor '{actorId}' owner changed before its Entry Spot update.")
                    : RestoreEntrySpot(actorId, payload),
                expectedActorRef: null,
                cancellationToken: cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask NotifyActorLeftSpotAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        await RenewOwnedActorAsync(
                actorId,
                payload => RestoreEntrySpot(actorId, payload),
                expectedActorRef: null,
                cancellationToken: cancellationToken)
            .ConfigureAwait(false);
    }

    private ZLinkActorAuthorityPayload RestoreEntrySpot(
        string actorId,
        ZLinkActorAuthorityPayload payload)
    {
        lock (_gate)
        {
            if (!_actors.TryGetValue(actorId, out var tracked)
                || string.IsNullOrEmpty(tracked.EntrySpotId))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorLocationStale,
                    $"Actor '{actorId}' Entry Spot identity is unavailable.");
            return payload with
            {
                CurrentSpotKind = ZLinkSpotKind.Entry,
                CurrentSpotId = tracked.EntrySpotId,
                CurrentSpotGeneration = tracked.EntrySpotGeneration
            };
        }
    }

    public async ValueTask ReleaseActorAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var canonical = actorId;
        TrackedActor tracked;
        TaskCompletionSource? releaseCompletion = null;
        Task releaseTask;
        lock (_gate)
        {
            if (!_actors.TryGetValue(canonical, out tracked!)) return;
            if (tracked.ReleaseTask is null)
            {
                releaseCompletion = new TaskCompletionSource(
                    TaskCreationOptions.RunContinuationsAsynchronously);
                tracked.ReleaseTask = releaseCompletion.Task;
            }

            releaseTask = tracked.ReleaseTask;
        }

        if (releaseCompletion is not null)
        {
            try
            {
                await ReleaseTrackedActorAsync(canonical, tracked).ConfigureAwait(false);
                releaseCompletion.TrySetResult();
            }
            catch (Exception exception)
            {
                lock (_gate)
                {
                    if (_actors.TryGetValue(canonical, out var current)
                        && ReferenceEquals(current, tracked)
                        && ReferenceEquals(current.ReleaseTask, releaseTask))
                        current.ReleaseTask = null;
                }

                releaseCompletion.TrySetException(exception);
            }
        }

        await releaseTask.WaitAsync(cancellationToken).ConfigureAwait(false);
    }

    internal bool OwnsActor(string actorId)
    {
        lock (_gate)
        {
            return _actors.ContainsKey(actorId);
        }
    }

    internal void ResetGeneration()
    {
        lock (_gate) _actors.Clear();
    }

    private void MarkActivationFailed(string actorId)
    {
        var canonical = actorId;
        lock (_gate)
        {
            if (_actors.TryGetValue(canonical, out var tracked))
                tracked.ActivationFailed = true;
        }
    }

    private void StartActivationFailureReconciliation(string actorId)
    {
        var canonical = actorId;
        TrackedActor? tracked;
        lock (_gate)
        {
            if (!_actors.TryGetValue(canonical, out tracked)
                || Interlocked.Exchange(ref tracked.ReconciliationStarted, 1) != 0)
                return;
            if (_disposed != 0 || _backgroundStopping)
            {
                Interlocked.Exchange(ref tracked.ReconciliationStarted, 0);
                return;
            }
            tracked.ReconciliationTask = ReconcileActivationFailureAsync(
                actorId,
                canonical,
                tracked,
                _reconciliationStop.Token);
        }
    }

    private async Task ReconcileActivationFailureAsync(
        string actorId,
        string canonical,
        TrackedActor tracked,
        CancellationToken stopToken)
    {
        try
        {
            await ZLinkReconciliationRunner.RunAsync(
                    token => ReleaseActorAsync(actorId, token),
                    exception => ZLinkFrameworkDebugLog.SpotDiscovery(
                        $"failed actor activation claim release retry for '{actorId}': {exception.Message}"),
                    stopToken)
                .ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (stopToken.IsCancellationRequested)
        {
        }
        finally
        {
            lock (_gate)
            {
                if (_actors.TryGetValue(canonical, out var current)
                    && ReferenceEquals(current, tracked))
                    Interlocked.Exchange(ref tracked.ReconciliationStarted, 0);
            }
        }
    }

    public ValueTask DisposeAsync()
    {
        lock (_disposeStartGate)
            return new ValueTask(_disposeTask ??= DisposeCoreAsync());
    }

    private async Task DisposeCoreAsync()
    {
        Interlocked.Exchange(ref _disposed, 1);
        _reconciliationStop.Cancel();
        await DrainBackgroundWorkCoreAsync().ConfigureAwait(false);

        TrackedActor[] trackedActors;
        lock (_gate)
        {
            trackedActors = _trackedActors.ToArray();
            _actors.Clear();
            _trackedActors.Clear();
        }

        foreach (var tracked in trackedActors)
        {
            await tracked.WriteGate.WaitAsync(CancellationToken.None).ConfigureAwait(false);
            tracked.WriteGate.Dispose();
        }

        _reconciliationStop.Dispose();
        _backgroundDrainGate.Dispose();
    }

    internal ValueTask PauseBackgroundWorkAsync()
        => DrainBackgroundWorkCoreAsync();

    internal void ResumeBackgroundWork()
    {
        CancellationTokenSource? stopped = null;
        lock (_gate)
        {
            if (_disposed != 0 || !_backgroundStopping) return;
            if (_reconciliationStop.IsCancellationRequested)
            {
                stopped = _reconciliationStop;
                _reconciliationStop = new CancellationTokenSource();
            }
            _backgroundStopping = false;
        }
        stopped?.Dispose();
    }

    private async ValueTask DrainBackgroundWorkCoreAsync()
    {
        await _backgroundDrainGate.WaitAsync(CancellationToken.None).ConfigureAwait(false);
        try
        {
            Task[] tasks;
            CancellationTokenSource stop;
            lock (_gate)
            {
                stop = _reconciliationStop;
                _backgroundStopping = true;
                if (!stop.IsCancellationRequested) stop.Cancel();
                tasks = _actors.Values
                    .SelectMany(static actor => new[] { actor.ReconciliationTask, actor.ReleaseTask })
                    .Where(static task => task is not null)
                    .Cast<Task>()
                    .Distinct()
                    .ToArray();
            }

            if (tasks.Length != 0)
                try
                {
                    await Task.WhenAll(tasks).ConfigureAwait(false);
                }
                catch (OperationCanceledException) when (stop.IsCancellationRequested)
                {
                }
                catch (Exception exception)
                {
                    ZLinkFrameworkDebugLog.SpotDiscovery(
                        $"actor ownership background drain failed: {exception.Message}");
                }

            lock (_gate)
            {
                _backgroundStopping = true;
            }
        }
        finally
        {
            _backgroundDrainGate.Release();
        }
    }

    internal Func<CancellationToken, ValueTask>? TakeOwnershipLostDeactivation(string canonicalKey)
    {
        // Ownership loss carries the exact authority key while local tracking
        // uses the framework-wide Actor id.
        lock (_gate)
        {
            foreach (var (actorId, actor) in _actors)
            {
                var encoded = ZLinkActorAuthorityPayloadCodec.AuthorityKey(actorId).Value;
                if (!string.Equals(encoded, canonicalKey, StringComparison.Ordinal)) continue;
                _actors.Remove(actorId);
                return actor.Deactivate;
            }

            return null;
        }
    }

    private async ValueTask RenewOwnedActorAsync(
        string actorId,
        Func<ZLinkActorAuthorityPayload, ZLinkActorAuthorityPayload> mutate,
        ActorRef? expectedActorRef,
        CancellationToken cancellationToken)
    {
        var canonical = actorId;
        TrackedActor? tracked;
        lock (_gate)
        {
            if (!_actors.TryGetValue(canonical, out tracked))
            {
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"Actor '{actorId}' is not tracked by this location owner.");
            }

            if (tracked.ReleaseTask is not null)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorRouteNotFound,
                    $"Actor '{actorId}' location is being released.");
        }

        await tracked.WriteGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            ZLinkActorAuthorityPayload proposed;
            ZLinkAuthoritySnapshot snapshot;
            lock (_gate)
            {
                if (!_actors.TryGetValue(canonical, out var current)
                    || !ReferenceEquals(current, tracked))
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorLocationStale,
                        $"Actor '{actorId}' is no longer tracked by this location owner.");
                if (tracked.ReleaseTask is not null)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorRouteNotFound,
                        $"Actor '{actorId}' location is being released.");

                snapshot = tracked.Snapshot;
                if (expectedActorRef is { } actorRef
                    && (actorRef.ObjectGeneration != snapshot.ObjectGeneration
                        || actorRef.NodeRid != tracked.Payload.NodeRid))
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorGenerationStale,
                        $"Actor '{actorId}' reference no longer matches its authority.");
                proposed = mutate(tracked.Payload);
            }

            if (ReferenceEquals(proposed, tracked.Payload))
                return;

            var authorityKey = ZLinkActorAuthorityPayloadCodec.AuthorityKey(actorId);
            var result = await runtime.Store.CompareExchangeAuthorityAsync(
                    authorityKey,
                    snapshot.StoreVersion,
                    new ZLinkAuthorityMutation.Put(
                        EncodeAuthorityPayload(snapshot.Payload, proposed),
                        ZLinkAuthorityGenerationTransition.Preserve,
                        TargetOwner: null,
                        RelocationCapacityFence: null),
                    cancellationToken)
                .ConfigureAwait(false);
            if (result is not ZLinkAuthorityCompareExchangeResult.Stored stored)
            {
                if (result is ZLinkAuthorityCompareExchangeResult.Conflict)
                    runtime.NotifyAuthorityOwnershipLost(
                        ZLinkLocationKind.Actor,
                        authorityKey);
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorLocationStale,
                    $"Actor '{actorId}' authority changed before its update.",
                    true);
            }

            lock (_gate)
            {
                if (_actors.TryGetValue(canonical, out var current)
                    && ReferenceEquals(current, tracked))
                {
                    tracked.Payload = proposed;
                    tracked.Snapshot = stored.Snapshot;
                }
            }
        }
        finally
        {
            tracked.WriteGate.Release();
        }
    }

    private async Task ReleaseTrackedActorAsync(
        string canonical,
        TrackedActor tracked)
    {
        await tracked.WriteGate.WaitAsync(CancellationToken.None).ConfigureAwait(false);
        try
        {
            ZLinkAuthoritySnapshot snapshot;
            lock (_gate)
            {
                if (!_actors.TryGetValue(canonical, out var current)
                    || !ReferenceEquals(current, tracked))
                    return;
                snapshot = tracked.Snapshot;
            }

            var key = ZLinkActorAuthorityPayloadCodec.AuthorityKey(canonical);
            var result = await runtime.Store.CompareExchangeAuthorityAsync(
                    key,
                    snapshot.StoreVersion,
                    new ZLinkAuthorityMutation.Delete(),
                    CancellationToken.None)
                .ConfigureAwait(false);
            if (result is not (ZLinkAuthorityCompareExchangeResult.Deleted
                or ZLinkAuthorityCompareExchangeResult.Conflict
                {
                    Current: ZLinkAuthorityReadResult.Missing
                }))
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.ActorLocationStale,
                    $"Actor '{canonical}' authority changed before release.",
                    true);

            lock (_gate)
            {
                if (_actors.TryGetValue(canonical, out var current)
                    && ReferenceEquals(current, tracked))
                    _actors.Remove(canonical);
            }
        }
        finally
        {
            tracked.WriteGate.Release();
        }
    }

    private static ReadOnlyMemory<byte> EncodeAuthorityPayload(
        ReadOnlyMemory<byte> current,
        ZLinkActorAuthorityPayload payload)
    {
        var applicationPayload = ZLinkActorAuthorityPayloadCodec.Encode(payload);
        if (ZLinkRelocationAuthorityPayloadCodec.TryDecode(
            current.Span,
            out var relocation))
        {
            var nested = ZLinkActorRelocationAuthorityPayloadCodec.TryDecode(
                relocation.ApplicationPayload.Span,
                out var phase)
                ? ZLinkActorRelocationAuthorityPayloadCodec.Encode(
                    phase with { ApplicationPayload = applicationPayload })
                : applicationPayload;
            return ZLinkRelocationAuthorityPayloadCodec.Encode(
                relocation with { ApplicationPayload = nested });
        }
        return ZLinkActorRelocationAuthorityPayloadCodec.TryDecode(
            current.Span,
            out var directPhase)
            ? ZLinkActorRelocationAuthorityPayloadCodec.Encode(
                directPhase with { ApplicationPayload = applicationPayload })
            : applicationPayload;
    }

    private static bool TryDecodeRelocationPhase(
        ReadOnlyMemory<byte> payload,
        out ZLinkActorRelocationAuthorityPayload phase,
        out ZLinkRelocationAuthorityPayload? publication)
    {
        if (ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                payload.Span,
                out var outer))
        {
            publication = outer;
            return ZLinkActorRelocationAuthorityPayloadCodec.TryDecode(
                outer.ApplicationPayload.Span,
                out phase);
        }
        publication = null;
        return ZLinkActorRelocationAuthorityPayloadCodec.TryDecode(
            payload.Span,
            out phase);
    }

    private static ReadOnlyMemory<byte> EncodeRelocationPhase(
        ZLinkActorRelocationAuthorityPayload phase,
        ZLinkRelocationAuthorityPayload? publication)
    {
        var encoded = ZLinkActorRelocationAuthorityPayloadCodec.Encode(phase);
        return publication is null
            ? encoded
            : ZLinkRelocationAuthorityPayloadCodec.Encode(
                publication with { ApplicationPayload = encoded });
    }

    private void UpdateTrackedSnapshot(
        string actorId,
        ZLinkAuthoritySnapshot snapshot)
    {
        lock (_gate)
        {
            if (_actors.TryGetValue(actorId, out var tracked))
                tracked.Snapshot = snapshot;
        }
    }

    private static ZLinkActorLocation ToLocation(
        ZLinkAuthoritySnapshot snapshot,
        ZLinkActorAuthorityPayload payload)
        => new(
            payload.MeshName,
            payload.ActorId,
            payload.StableType,
            new ActorRef(
                payload.ActorId,
                snapshot.ObjectGeneration,
                payload.MeshName,
                payload.NodeRid),
            payload.NodeRid,
            payload.NodeGeneration,
            payload.CurrentSpotId,
            payload.CurrentSpotGeneration,
            payload.CurrentSpotKind,
            payload.OwnerId,
            snapshot.OwnerLeaseGeneration,
            snapshot.StoreNow);

    private sealed class TrackedActor(
        ZLinkAuthoritySnapshot snapshot,
        ZLinkActorAuthorityPayload payload,
        Func<CancellationToken, ValueTask>? deactivate)
    {
        public ZLinkAuthoritySnapshot Snapshot { get; set; } = snapshot;

        public ZLinkActorAuthorityPayload Payload { get; set; } = payload;

        public string EntrySpotId { get; } = payload.CurrentSpotKind == ZLinkSpotKind.Entry
            ? payload.CurrentSpotId
            : string.Empty;

        public ulong EntrySpotGeneration { get; } =
            payload.CurrentSpotKind == ZLinkSpotKind.Entry
                ? payload.CurrentSpotGeneration
                : 0;

        public Func<CancellationToken, ValueTask>? Deactivate { get; } = deactivate;

        public SemaphoreSlim WriteGate { get; } = new(1, 1);

        public Task? ReleaseTask { get; set; }

        public Task? ReconciliationTask { get; set; }

        public bool ActivationFailed { get; set; }

        public int ReconciliationStarted;
    }
}
