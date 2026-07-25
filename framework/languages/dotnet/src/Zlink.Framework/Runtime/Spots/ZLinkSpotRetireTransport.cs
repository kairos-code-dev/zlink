using Microsoft.Extensions.DependencyInjection;
using System.Collections.Concurrent;
using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;

namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkSpotRetireProtocol
{
    internal const string Stage = "zlink.internal.spot.relocation.stage.v1";
    internal const string Publish = "zlink.internal.spot.relocation.publish.v1";
    internal const string Abort = "zlink.internal.spot.relocation.abort.v1";
    internal const string HeldRelay =
        "zlink.internal.spot.relocation.held-relay.v1";
}
internal sealed record ZLinkSpotRetireActorDescriptor(
    string ActorId,
    string StableType,
    byte[] AuthorityPayload);

[ZLinkPacket(ZLinkSpotRetireProtocol.Stage)]
internal sealed record ZLinkSpotRetireStageRequest(
    Guid AggregateId,
    ulong AggregateGeneration,
    string MeshName,
    string SourceNodeRid,
    ulong SourceNodeLifecycleGeneration,
    string SourceOwnerId,
    ulong SourceOwnerLeaseGeneration,
    string TargetNodeRid,
    ulong TargetNodeLifecycleGeneration,
    string TargetOwnerId,
    ulong TargetOwnerLeaseGeneration,
    string SpotId,
    string StableType,
    bool InstanceSpot,
    string RelocationReference,
    uint RelocationChecksum,
    ZLinkSpotRetireActorDescriptor[] Actors);

internal sealed record ZLinkSpotRetireReply(bool Acknowledged);

[ZLinkPacket(ZLinkSpotRetireProtocol.Publish)]
internal sealed record ZLinkSpotRetirePublishRequest(
    Guid AggregateId,
    ulong AggregateGeneration);

[ZLinkPacket(ZLinkSpotRetireProtocol.Abort)]
internal sealed record ZLinkSpotRetireAbortRequest(
    Guid AggregateId,
    ulong AggregateGeneration);

internal sealed record ZLinkSpotRetireHeldRecord(
    ulong AcceptedSequence,
    byte[] Payload);

[ZLinkPacket(ZLinkSpotRetireProtocol.HeldRelay)]
internal sealed record ZLinkSpotRetireHeldRelay(
    Guid AggregateId,
    ulong AggregateGeneration,
    string SpotId,
    ulong ObjectGeneration,
    ulong SourceAuthorityOwnerGeneration,
    ulong TargetAuthorityOwnerGeneration,
    ulong SourceNodeLifecycleGeneration,
    string SourceOwnerId,
    ulong SourceOwnerLeaseGeneration,
    ulong TargetNodeLifecycleGeneration,
    string TargetOwnerId,
    ulong TargetOwnerLeaseGeneration,
    int HopCount,
    ZLinkSpotRetireHeldRecord[] Records);

internal sealed class ZLinkSpotRetireStageHandler(
    ZLinkSpotRetireTargetRuntime target)
    : IZLinkRouteRequestHandler<
        ZLinkSpotRetireStageRequest,
        ZLinkSpotRetireReply>
{
    public ValueTask<ZLinkSpotRetireReply> HandleAsync(
        ZLinkSpotRetireStageRequest request,
        ZLinkRouteMessageContext context,
        CancellationToken cancellationToken) =>
        target.StageInboundAsync(
            request,
            context.SourceNodeRid,
            cancellationToken);
}

internal sealed class ZLinkSpotRetirePublishHandler(
    ZLinkSpotRetireTargetRuntime target)
    : IZLinkRouteRequestHandler<
        ZLinkSpotRetirePublishRequest,
        ZLinkSpotRetireReply>
{
    public ValueTask<ZLinkSpotRetireReply> HandleAsync(
        ZLinkSpotRetirePublishRequest request,
        ZLinkRouteMessageContext context,
        CancellationToken cancellationToken)
    {
        return target.PublishInboundAsync(
            request,
            context.SourceNodeRid,
            cancellationToken);
    }
}

internal sealed class ZLinkSpotRetireAbortHandler(
    ZLinkSpotRetireTargetRuntime target)
    : IZLinkRouteRequestHandler<
        ZLinkSpotRetireAbortRequest,
        ZLinkSpotRetireReply>
{
    public ValueTask<ZLinkSpotRetireReply> HandleAsync(
        ZLinkSpotRetireAbortRequest request,
        ZLinkRouteMessageContext context,
        CancellationToken cancellationToken)
    {
        return target.AbortInboundAsync(
            request,
            context.SourceNodeRid,
            cancellationToken);
    }
}

internal sealed class ZLinkSpotRetireHeldRelayHandler(
    ZLinkSpotRetireTargetRuntime target)
    : IZLinkRouteRequestHandler<
        ZLinkSpotRetireHeldRelay,
        ZLinkSpotRetireReply>
{
    public ValueTask<ZLinkSpotRetireReply> HandleAsync(
        ZLinkSpotRetireHeldRelay message,
        ZLinkRouteMessageContext context,
        CancellationToken cancellationToken) =>
        target.RelayInboundAsync(
            message,
            context.SourceNodeRid,
            cancellationToken);
}

/// <summary>
/// Implements both the source service-wire client and target staging journal.
/// Target state is keyed by aggregate fence, so duplicate Stage/Publish/Abort
/// requests are idempotent.
/// </summary>
internal sealed class ZLinkSpotRetireTargetRuntime(
    IServiceProvider services,
    ZLinkFrameworkRuntime runtime,
    ZLinkFrameworkRegistration registration) : IZLinkSpotRetireTarget
{
    private const int MaxStagedAggregates = 1_024;
    internal static readonly TimeSpan StageRetention = TimeSpan.FromHours(24);
    private readonly ConcurrentDictionary<
        ZLinkAggregateFence,
        TargetStage> _staged = new();
    private int _stagedCount;

    public async ValueTask<ZLinkSpotRetireReservation?> TryReserveAsync(
        ZLinkSpotRetireInventory inventory,
        CancellationToken cancellationToken)
    {
        if (services.GetService<IZLinkMeshNodeLocationResolver>() is not { } resolver)
            return null;
        var descriptors = await resolver.ListLiveMeshNodesAsync(
                inventory.MeshName,
                cancellationToken)
            .ConfigureAwait(false);
        var kind = inventory.InstanceSpot
            ? ZLinkPlacementObjectKind.InstanceSpot
            : ZLinkPlacementObjectKind.UserSpot;
        var target = descriptors
            .Where(candidate =>
                candidate.Rid != inventory.SourceNodeRid
                && candidate.State == ZLinkFrameworkRuntimeState.Serving
                && candidate.ObjectRole == ZLinkMeshNodeObjectRole.Server
                && candidate.PlacementWeight > 0
                && candidate.ObjectCapabilities.Any(capability =>
                    capability.ObjectKind == kind
                    && capability.StableType == inventory.StableType))
            .OrderBy(static candidate => candidate.Rid.ToHex(), StringComparer.Ordinal)
            .FirstOrDefault();
        if (target is null || target.LeaseGeneration <= 0)
            return null;
        return new ZLinkSpotRetireReservation(
            inventory,
            new ZLinkMeshNodeDescriptorKey(target.MeshName, target.Rid),
            target.LifecycleGeneration,
            new ZLinkCapacityVector(
                inventory.ActorIds.Count,
                1,
                new ZLinkSpotTypeCapacityDelta(
                    kind,
                    inventory.StableType,
                    1)),
            new ZLinkLocationOwnerToken(
                target.OwnerId,
                checked((ulong)target.LeaseGeneration)));
    }

    public async ValueTask StageAsync(
        ZLinkSpotRetireReservation reservation,
        ZLinkPreparedAggregateRelocation relocation,
        CancellationToken cancellationToken)
    {
        var actors = relocation.Participants
            .Where(static participant =>
                participant.Envelope.ObjectKind == ZLinkPlacementObjectKind.Actor)
            .Select(participant =>
            {
                if (!ZLinkActorAuthorityPayloadCodec.TryDecodeRelocating(
                        participant.ApplicationAuthorityPayload.Span,
                        out var actor))
                    throw new InvalidDataException(
                        $"Actor relocation authority '{participant.Envelope.AuthorityKey.Value}' is invalid.");
                return new ZLinkSpotRetireActorDescriptor(
                    actor.ActorId,
                    actor.StableType,
                    participant.ApplicationAuthorityPayload.ToArray());
            })
            .ToArray();
        var reply = await runtime.RouteClient
            .RequestToNode(
                reservation.TargetDescriptor.MeshName,
                reservation.TargetDescriptor.Rid,
                new ZLinkSpotRetireStageRequest(
                    relocation.Envelope.AggregateId,
                    relocation.Envelope.AggregateGeneration,
                    reservation.TargetDescriptor.MeshName,
                    reservation.Inventory.SourceNodeRid.ToHex(),
                    reservation.Inventory.SourceNodeLifecycleGeneration,
                    reservation.Inventory.SourceOwner.OwnerId,
                    checked((ulong)reservation.Inventory.SourceOwner
                        .LeaseGeneration),
                    reservation.TargetDescriptor.Rid.ToHex(),
                    reservation.TargetDescriptorLifecycleGeneration,
                    reservation.TargetOwner.OwnerId,
                    checked((ulong)reservation.TargetOwner.LeaseGeneration),
                    reservation.Inventory.SpotId,
                    reservation.Inventory.StableType,
                    reservation.Inventory.InstanceSpot,
                    relocation.Relocation.Reference,
                    relocation.Relocation.ChecksumCrc32c,
                    actors))
            .Async<ZLinkSpotRetireReply>(cancellationToken)
            .ConfigureAwait(false);
        if (!reply.Acknowledged)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SpotMoving,
                $"Target did not stage SPOT '{reservation.Inventory.SpotId}'.",
                true);
    }

    public async ValueTask PublishAsync(
        ZLinkSpotRetireReservation reservation,
        ZLinkAggregateRelocationPublished relocation,
        CancellationToken cancellationToken)
    {
        var reply = await runtime.RouteClient
            .RequestToNode(
                reservation.TargetDescriptor.MeshName,
                reservation.TargetDescriptor.Rid,
                new ZLinkSpotRetirePublishRequest(
                    relocation.Fence.AggregateId,
                    relocation.Fence.AggregateGeneration))
            .Async<ZLinkSpotRetireReply>(cancellationToken)
            .ConfigureAwait(false);
        if (!reply.Acknowledged)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SpotMoving,
                $"Target did not publish SPOT '{reservation.Inventory.SpotId}'.",
                true);
    }

    public async ValueTask AbortAsync(
        ZLinkSpotRetireReservation reservation,
        ZLinkAggregateFence? fence)
    {
        if (fence is null)
            return;
        var exactFence = fence.Value;
        var reply = await runtime.RouteClient
            .RequestToNode(
                reservation.TargetDescriptor.MeshName,
                reservation.TargetDescriptor.Rid,
                new ZLinkSpotRetireAbortRequest(
                    exactFence.AggregateId,
                    exactFence.AggregateGeneration))
            .Async<ZLinkSpotRetireReply>(CancellationToken.None)
            .ConfigureAwait(false);
        if (!reply.Acknowledged)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SpotMoving,
                $"Target did not acknowledge abort for relocation '{exactFence.AggregateId}'.",
                true);
    }

    public ValueTask RelayCommittedAsync(
        ZLinkSpotRetireReservation reservation,
        ZLinkAggregateRelocationPublished relocation,
        IReadOnlyList<ZLinkAcceptedWorkRecord> held,
        CancellationToken cancellationToken)
    {
        // Captured records are replayed from the immutable root. Held ingress
        // is sent only after authority commit by the object-routing forwarding
        // mapping; no Location Store lookup or automatic resubmit is allowed.
        return runtime.RelayCommittedSpotRecordsAsync(
            reservation,
            relocation,
            held,
            cancellationToken);
    }

    internal async ValueTask RecoverPublishedAsync(
        ZLinkRelocationRecoveryCandidate candidate,
        CancellationToken cancellationToken)
    {
        var spotAuthority = candidate.Authorities.Single(
            static entry => entry.Snapshot.Allocation.ObjectKind
                is ZLinkPlacementObjectKind.UserSpot
                or ZLinkPlacementObjectKind.InstanceSpot);
        var durableSpot = candidate.Envelope.Participants.Single(
            static participant => participant.ObjectKind
                is ZLinkPlacementObjectKind.UserSpot
                or ZLinkPlacementObjectKind.InstanceSpot);
        var completionFinalized =
            ZLinkSpotRetireCompletionMarker.IsCompleted(
                durableSpot.CompletionPayload.Span);
        if (!ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                spotAuthority.Snapshot.Payload.Span,
                out var spotPublication))
            throw new ZLinkRelocationDataLostException(
                $"SPOT authority '{spotAuthority.Key.Value}' has no relocation publication.");

        var instanceSpot =
            spotAuthority.Snapshot.Allocation.ObjectKind
            == ZLinkPlacementObjectKind.InstanceSpot;
        string spotId;
        string meshName;
        RoutingId sourceNodeRid;
        ulong sourceNodeGeneration;
        string sourceOwnerId;
        ulong sourceOwnerGeneration;
        if (instanceSpot)
        {
            if (!ZLinkInstanceSpotAuthorityPayloadCodec.TryDecode(
                    spotPublication.ApplicationPayload.Span,
                    out var source))
                throw new ZLinkRelocationDataLostException(
                    "Instance SPOT relocation source authority is invalid.");
            spotId = source.SpotId;
            meshName = source.MeshName;
            sourceNodeRid = source.NodeRid;
            sourceNodeGeneration = source.NodeGeneration;
            sourceOwnerId = source.OwnerId;
            sourceOwnerGeneration = source.OwnerLeaseGeneration;
        }
        else
        {
            if (!ZLinkUserSpotAuthorityPayloadCodec.TryDecode(
                    spotPublication.ApplicationPayload.Span,
                    out var source))
                throw new ZLinkRelocationDataLostException(
                    "User SPOT relocation source authority is invalid.");
            spotId = source.SpotId;
            meshName = source.MeshName;
            sourceNodeRid = source.NodeRid;
            sourceNodeGeneration = source.NodeGeneration;
            sourceOwnerId = source.OwnerId;
            sourceOwnerGeneration = source.OwnerLeaseGeneration;
        }

        var actorAuthorities = candidate.Authorities
            .Where(static entry =>
                entry.Snapshot.Allocation.ObjectKind
                == ZLinkPlacementObjectKind.Actor)
            .ToDictionary(
                static entry => entry.Key.Value,
                StringComparer.Ordinal);
        var actors = candidate.Envelope.Participants
            .Where(static participant =>
                participant.ObjectKind == ZLinkPlacementObjectKind.Actor)
            .Select(participant =>
            {
                var authority = actorAuthorities[
                    participant.AuthorityKey.Value];
                if (!ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                        authority.Snapshot.Payload.Span,
                        out var publication)
                    || !ZLinkActorAuthorityPayloadCodec.TryDecodeRelocating(
                        publication.ApplicationPayload.Span,
                        out var actor))
                    throw new ZLinkRelocationDataLostException(
                        $"Actor authority '{authority.Key.Value}' is invalid.");
                return new ZLinkSpotRetireActorDescriptor(
                    actor.ActorId,
                    actor.StableType,
                    publication.ApplicationPayload.ToArray());
            })
            .ToArray();
        ZLinkSpotNodeRuntime recoveryNode;
        try
        {
            recoveryNode = runtime.GetSpotNodeRuntime(
                spotAuthority.Snapshot.Allocation.Descriptor.Rid);
        }
        catch (ZLinkFrameworkException exception)
            when (exception.Kind
                  == ZLinkFrameworkErrorKind.ActorRouteNotFound)
        {
            return;
        }
        var request = new ZLinkSpotRetireStageRequest(
            candidate.Envelope.AggregateId,
            candidate.Envelope.AggregateGeneration,
            meshName,
            sourceNodeRid.ToHex(),
            sourceNodeGeneration,
            sourceOwnerId,
            sourceOwnerGeneration,
            spotAuthority.Snapshot.Allocation.Descriptor.Rid.ToHex(),
            spotAuthority.Snapshot.Allocation.DescriptorLifecycleGeneration,
            spotAuthority.Snapshot.OwnerId,
            checked((ulong)spotAuthority.Snapshot.OwnerLeaseGeneration),
            spotId,
            spotAuthority.Snapshot.Allocation.StableType,
            instanceSpot,
            candidate.Reference.Reference,
            candidate.Reference.ChecksumCrc32c,
            actors);
        var fence = new ZLinkAggregateFence(
            candidate.Envelope.AggregateId,
            candidate.Envelope.AggregateGeneration);
        if (_staged.TryGetValue(fence, out var existing))
        {
            if (!existing.Matches(request, sourceNodeRid))
                throw new ZLinkRelocationDataLostException(
                    "Recovered SPOT staging conflicts with its published aggregate.");
            Volatile.Write(ref existing.AuthorityPublished, 1);
            if (!completionFinalized)
                completionFinalized = await TryPromotePendingRecoveryAsync(
                        candidate,
                        existing,
                        spotAuthority.Snapshot,
                        meshName,
                        sourceNodeRid,
                        sourceNodeGeneration,
                        sourceOwnerId,
                        sourceOwnerGeneration,
                        cancellationToken)
                    .ConfigureAwait(false);
            if (completionFinalized)
            {
                await FinalizeStageAsync(existing, cancellationToken)
                    .ConfigureAwait(false);
                existing.ReleasePermit();
            }
            return;
        }

        var encodedLength = ZLinkRelocationEnvelopeCodec.MeasureEncodedLength(
            candidate.Envelope);
        if (!TryReserveStageSlot())
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SpotMoving,
                $"Inbound recovery staging is full for SPOT '{spotId}'.",
                true);
        if (!runtime.TryAcquireInboundSpotRelocation(
                encodedLength,
                RequiresRestore(recoveryNode, request),
                allowOversizedPayload: !request.InstanceSpot,
                out var permit))
        {
            ReleaseStageSlot();
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SpotMoving,
                $"Inbound recovery permit is unavailable for SPOT '{spotId}'.",
                true);
        }
        TargetStage stage;
        try
        {
            stage = await runtime.StageInboundSpotAggregateAsync(
                    request,
                    candidate.Envelope,
                    permit,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch
        {
            permit.Dispose();
            ReleaseStageSlot();
            throw;
        }
        if (!_staged.TryAdd(fence, stage))
        {
            await runtime.AbortInboundSpotAggregateAsync(stage)
                .ConfigureAwait(false);
            stage.ReleasePermit();
            ReleaseStageSlot();
            if (!_staged.TryGetValue(fence, out var winner)
                || !winner.Matches(request, sourceNodeRid))
                throw new ZLinkRelocationDataLostException(
                    "Recovered SPOT staging conflicts with the aggregate winner.");
            stage = winner;
        }
        Volatile.Write(ref stage.AuthorityPublished, 1);
        if (!completionFinalized)
            completionFinalized = await TryPromotePendingRecoveryAsync(
                    candidate,
                    stage,
                    spotAuthority.Snapshot,
                    meshName,
                    sourceNodeRid,
                    sourceNodeGeneration,
                    sourceOwnerId,
                    sourceOwnerGeneration,
                    cancellationToken)
                .ConfigureAwait(false);
        if (completionFinalized)
        {
            await FinalizeStageAsync(stage, cancellationToken)
                .ConfigureAwait(false);
            stage.ReleasePermit();
        }
    }

    private async ValueTask<bool> TryPromotePendingRecoveryAsync(
        ZLinkRelocationRecoveryCandidate candidate,
        TargetStage stage,
        ZLinkAuthoritySnapshot spotAuthority,
        string meshName,
        RoutingId sourceNodeRid,
        ulong sourceNodeGeneration,
        string sourceOwnerId,
        ulong sourceOwnerLeaseGeneration,
        CancellationToken cancellationToken)
    {
        var spot = candidate.Envelope.Participants.Single(
            static participant => participant.ObjectKind
                is ZLinkPlacementObjectKind.UserSpot
                or ZLinkPlacementObjectKind.InstanceSpot);
        if (!ZLinkSpotRetireCompletionMarker.IsPending(
                spot.CompletionPayload.Span))
            throw new ZLinkRelocationDataLostException(
                "Relocation recovery root has an unknown source-cleanup phase.");

        // Replay is completed while target admission remains sealed. It is
        // safe to publish Completed only after the exact source lifecycle can
        // no longer execute its queue.
        await runtime.PrepareInboundSpotAggregateAsync(
                stage,
                cancellationToken)
            .ConfigureAwait(false);
        if (!await IsSourceLifecycleFencedAsync(
                meshName,
                sourceNodeRid,
                sourceNodeGeneration,
                sourceOwnerId,
                sourceOwnerLeaseGeneration,
                cancellationToken).ConfigureAwait(false))
            return false;

        var targetOwner = new ZLinkLocationOwnerToken(
            spotAuthority.OwnerId,
            checked((ulong)spotAuthority.OwnerLeaseGeneration));
        await new ZLinkAggregateRelocationCoordinator(
                registration.Locations.ResolveStore()
                ?? throw new ZLinkConfigurationException(
                    "Location Store is not registered."),
                registration.Locations.RelocationStoreInstance
                ?? throw new ZLinkConfigurationException(
                    "Relocation Store is not registered."))
            .CompleteSourceCleanupAsync(
                new ZLinkAggregateRelocationPublished(
                    new ZLinkAggregateFence(
                        candidate.Envelope.AggregateId,
                        candidate.Envelope.AggregateGeneration),
                    new ZLinkRelocationStored(
                        candidate.Reference.Reference,
                        candidate.Reference.ChecksumCrc32c,
                        default,
                        spotAuthority.StoreNow),
                    candidate.Envelope),
                spotAuthority.Allocation.Descriptor,
                spotAuthority.Allocation.DescriptorLifecycleGeneration,
                targetOwner,
                cancellationToken)
            .ConfigureAwait(false);
        return true;
    }

    private async ValueTask<bool> IsSourceLifecycleFencedAsync(
        string meshName,
        RoutingId sourceNodeRid,
        ulong sourceNodeGeneration,
        string sourceOwnerId,
        ulong sourceOwnerLeaseGeneration,
        CancellationToken cancellationToken)
    {
        var resolver = services.GetService<IZLinkMeshNodeLocationResolver>();
        if (resolver is null)
            return false;
        var live = await resolver.ListLiveMeshNodesAsync(
                meshName,
                cancellationToken)
            .ConfigureAwait(false);
        return !IsExactSourceLifecycleStillLive(
            live,
            sourceNodeRid,
            sourceNodeGeneration,
            sourceOwnerId,
            sourceOwnerLeaseGeneration);
    }

    internal static bool IsExactSourceLifecycleStillLive(
        IReadOnlyList<ZLinkMeshNodeDescriptor> live,
        RoutingId sourceNodeRid,
        ulong sourceNodeGeneration,
        string sourceOwnerId,
        ulong sourceOwnerLeaseGeneration) =>
        live.Any(descriptor =>
            descriptor.Rid == sourceNodeRid
            && descriptor.LifecycleGeneration == sourceNodeGeneration
            && descriptor.OwnerId == sourceOwnerId
            && descriptor.LeaseGeneration > 0
            && checked((ulong)descriptor.LeaseGeneration)
               == sourceOwnerLeaseGeneration);

    internal async ValueTask<ZLinkSpotRetireReply> RelayInboundAsync(
        ZLinkSpotRetireHeldRelay message,
        RoutingId sourceNodeRid,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var fence = new ZLinkAggregateFence(
            message.AggregateId,
            message.AggregateGeneration);
        if (_staged.TryGetValue(fence, out var expired)
            && expired.ExpiresAt <= DateTimeOffset.UtcNow
            && Volatile.Read(ref expired.Published) != 0
            && _staged.TryRemove(fence, out expired))
        {
            expired.ReleasePermit();
            ReleaseStageSlot();
            return new ZLinkSpotRetireReply(false);
        }
        if (!_staged.TryGetValue(fence, out var stage)
            || Volatile.Read(ref stage.AuthorityPublished) == 0
            || stage.SourceNodeRid != sourceNodeRid
            || stage.Spot.Activation.SpotId != message.SpotId
            || stage.Spot.Activation.ObjectGeneration
               != message.ObjectGeneration
            || stage.SourceAuthorityOwnerGeneration
               != message.SourceAuthorityOwnerGeneration
            || stage.TargetAuthorityOwnerGeneration
               != message.TargetAuthorityOwnerGeneration
            || stage.SourceNodeLifecycleGeneration
               != message.SourceNodeLifecycleGeneration
            || stage.SourceOwner.OwnerId != message.SourceOwnerId
            || stage.SourceOwner.LeaseGeneration <= 0
            || checked((ulong)stage.SourceOwner.LeaseGeneration)
               != message.SourceOwnerLeaseGeneration
            || stage.TargetNodeLifecycleGeneration
               != message.TargetNodeLifecycleGeneration
            || stage.TargetOwnerLeaseGeneration
               != message.TargetOwnerLeaseGeneration
            || runtime.LocationLifecycle?.OwnerToken is not { } targetOwner
            || targetOwner.OwnerId != message.TargetOwnerId
            || targetOwner.LeaseGeneration <= 0
            || checked((ulong)targetOwner.LeaseGeneration)
               != message.TargetOwnerLeaseGeneration
            || message.TargetAuthorityOwnerGeneration
               != checked(message.SourceAuthorityOwnerGeneration + 1)
            || message.HopCount is < 1 or > 8)
            return new ZLinkSpotRetireReply(false);

        ValidateHeldRecords(message.Records);
        var digest = ComputeHeldDigest(message.Records);
        lock (stage.HeldGate)
        {
            if (stage.HeldDigest is { } priorDigest)
            {
                if (!priorDigest.AsSpan().SequenceEqual(digest))
                    return new ZLinkSpotRetireReply(false);
            }
            else
            {
                stage.HeldDigest = digest;
                stage.HeldRecords = message.Records.Select(
                        static record => new ZLinkRelocationQueuedJob(
                            record.AcceptedSequence,
                            record.Payload.ToArray()))
                    .ToArray();
                stage.HeldHighWater = message.Records.Length == 0
                    ? 0
                    : message.Records[^1].AcceptedSequence;
            }
        }
        await FinalizeStageAsync(stage, cancellationToken)
            .ConfigureAwait(false);
        stage.ReleasePermit();
        return new ZLinkSpotRetireReply(true);
    }

    internal static void ValidateHeldRecords(
        IReadOnlyList<ZLinkSpotRetireHeldRecord> records)
    {
        if (records.Count > 1_024)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestRejected,
                "A SPOT relocation forwarding queue cannot exceed 1,024 messages.");
        long bytes = 0;
        ulong previous = 0;
        foreach (var record in records)
        {
            if (record.AcceptedSequence == 0
                || record.AcceptedSequence <= previous)
                throw new InvalidDataException(
                    "Forwarded SPOT ingress sequences must be strictly increasing.");
            previous = record.AcceptedSequence;
            bytes = checked(bytes + record.Payload.LongLength);
            if (bytes > 16L * 1024 * 1024)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RequestRejected,
                    "A SPOT relocation forwarding queue cannot exceed 16 MiB.");
        }
    }

    internal static byte[] ComputeStageRequestDigest(
        ZLinkSpotRetireStageRequest request)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        Append(request.AggregateId.ToString("N"));
        Append(request.AggregateGeneration.ToString());
        Append(request.MeshName);
        Append(request.SourceNodeRid);
        Append(request.SourceNodeLifecycleGeneration.ToString());
        Append(request.SourceOwnerId);
        Append(request.SourceOwnerLeaseGeneration.ToString());
        Append(request.TargetNodeRid);
        Append(request.TargetNodeLifecycleGeneration.ToString());
        Append(request.TargetOwnerId);
        Append(request.TargetOwnerLeaseGeneration.ToString());
        Append(request.SpotId);
        Append(request.StableType);
        Append(request.InstanceSpot ? "1" : "0");
        Append(request.RelocationReference);
        Append(request.RelocationChecksum.ToString());
        Span<byte> payloadLength = stackalloc byte[4];
        foreach (var actor in request.Actors)
        {
            Append(actor.ActorId);
            Append(actor.StableType);
            BinaryPrimitives.WriteInt32BigEndian(
                payloadLength,
                actor.AuthorityPayload.Length);
            hash.AppendData(payloadLength);
            hash.AppendData(actor.AuthorityPayload);
        }
        return hash.GetHashAndReset();

        void Append(string value)
        {
            var bytes = Encoding.UTF8.GetBytes(value);
            Span<byte> length = stackalloc byte[4];
            BinaryPrimitives.WriteInt32BigEndian(length, bytes.Length);
            hash.AppendData(length);
            hash.AppendData(bytes);
        }
    }

    internal async ValueTask<ZLinkSpotRetireReply> StageInboundAsync(
        ZLinkSpotRetireStageRequest request,
        RoutingId sourceNodeRid,
        CancellationToken cancellationToken)
    {
        await CleanupExpiredAsync().ConfigureAwait(false);
        if (sourceNodeRid.ToHex() != request.SourceNodeRid)
            return new ZLinkSpotRetireReply(false);
        var targetNode = runtime.GetSpotNodeRuntime(
            RoutingId.FromHex(request.TargetNodeRid));
        var targetStatus = targetNode.Node.MeshStatus();
        var localOwner = runtime.LocationLifecycle?.OwnerToken;
        if (request.SourceNodeLifecycleGeneration == 0
            || request.SourceOwnerLeaseGeneration == 0
            || string.IsNullOrWhiteSpace(request.SourceOwnerId)
            || targetStatus.MeshName != request.MeshName
            || targetStatus.RoutingId.ToHex() != request.TargetNodeRid
            || targetStatus.LifecycleGeneration
               != request.TargetNodeLifecycleGeneration
            || localOwner is not { } exactLocalOwner
            || exactLocalOwner.OwnerId != request.TargetOwnerId
            || exactLocalOwner.LeaseGeneration
               != checked((long)request.TargetOwnerLeaseGeneration))
            return new ZLinkSpotRetireReply(false);
        var fence = new ZLinkAggregateFence(
            request.AggregateId,
            request.AggregateGeneration);
        if (_staged.TryGetValue(fence, out var existing))
            return new ZLinkSpotRetireReply(
                existing.Matches(request, sourceNodeRid));
        var relocationStore = registration.Locations.RelocationStoreInstance
                              ?? throw new ZLinkConfigurationException(
                                  "Relocation Store is not registered.");
        var tree = await ZLinkRelocationTreeStore.ReadAsync(
                relocationStore,
                request.RelocationReference,
                request.RelocationChecksum,
                cancellationToken)
            .ConfigureAwait(false);
        var envelope = tree.Envelope;
        if (envelope.AggregateId != request.AggregateId
            || envelope.AggregateGeneration != request.AggregateGeneration)
            throw new InvalidDataException(
                "SPOT relocation aggregate fence does not match its root.");
        if (!ValidateActorDescriptors(envelope, request))
            return new ZLinkSpotRetireReply(false);
        if (!await IsSourceFenceCurrentAsync(
                request,
                envelope,
                cancellationToken).ConfigureAwait(false))
            return new ZLinkSpotRetireReply(false);
        if (!TryReserveStageSlot())
            return new ZLinkSpotRetireReply(false);
        if (!runtime.TryAcquireInboundSpotRelocation(
                tree.LogicalLength,
                RequiresRestore(targetNode, request),
                allowOversizedPayload: !request.InstanceSpot,
                out var inboundPermit))
        {
            ReleaseStageSlot();
            return new ZLinkSpotRetireReply(false);
        }
        TargetStage stage;
        try
        {
            stage = await runtime.StageInboundSpotAggregateAsync(
                    request,
                    envelope,
                    inboundPermit,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch
        {
            inboundPermit.Dispose();
            ReleaseStageSlot();
            throw;
        }
        if (!_staged.TryAdd(fence, stage))
        {
            await runtime.AbortInboundSpotAggregateAsync(stage)
                .ConfigureAwait(false);
            stage.ReleasePermit();
            ReleaseStageSlot();
            return new ZLinkSpotRetireReply(
                _staged.TryGetValue(fence, out var winner)
                && winner.Matches(request, sourceNodeRid));
        }
        return new ZLinkSpotRetireReply(true);
    }

    private async ValueTask<bool> IsSourceFenceCurrentAsync(
        ZLinkSpotRetireStageRequest request,
        ZLinkRelocationEnvelope envelope,
        CancellationToken cancellationToken)
    {
        var authorityStore = registration.Locations.ResolveStore();
        if (authorityStore is null)
            return false;
        var spot = envelope.Participants.Single(
            static participant => participant.ObjectKind
                is ZLinkPlacementObjectKind.UserSpot
                or ZLinkPlacementObjectKind.InstanceSpot);
        ZLinkAuthoritySnapshot? spotSnapshot = null;
        foreach (var participant in envelope.Participants)
        {
            var read = await authorityStore.ReadAuthorityAsync(
                    participant.AuthorityKey,
                    cancellationToken)
                .ConfigureAwait(false);
            if (read is not ZLinkAuthorityReadResult.Found found
                || found.Snapshot.ObjectGeneration
                   != participant.ObjectGeneration
                || found.Snapshot.AuthorityOwnerGeneration
                   != participant.AuthorityOwnerGeneration
                || found.Snapshot.OwnerId != request.SourceOwnerId
                || found.Snapshot.OwnerLeaseGeneration <= 0
                || checked((ulong)found.Snapshot.OwnerLeaseGeneration)
                   != request.SourceOwnerLeaseGeneration)
                return false;

            if (participant.ObjectKind == ZLinkPlacementObjectKind.Actor)
            {
                if (!ZLinkActorAuthorityPayloadCodec.TryDecode(
                        found.Snapshot.Payload.Span,
                        out var actor)
                    || actor.MeshName != request.MeshName
                    || actor.NodeRid.ToHex() != request.SourceNodeRid
                    || actor.NodeGeneration
                       != request.SourceNodeLifecycleGeneration)
                    return false;
            }
            else
            {
                spotSnapshot = found.Snapshot;
            }
        }
        if (spotSnapshot is null)
            return false;
        if (request.InstanceSpot)
            return ZLinkInstanceSpotAuthorityPayloadCodec.TryDecode(
                       spotSnapshot.Payload.Span,
                   out var instance)
                   && instance.SpotId == request.SpotId
                   && instance.StableType == request.StableType
                   && instance.MeshName == request.MeshName
                   && instance.NodeRid.ToHex() == request.SourceNodeRid
                   && instance.NodeGeneration
                      == request.SourceNodeLifecycleGeneration;
        return ZLinkUserSpotAuthorityPayloadCodec.TryDecode(
                   spotSnapshot.Payload.Span,
               out var user)
               && user.SpotId == request.SpotId
               && user.StableType == request.StableType
               && user.MeshName == request.MeshName
               && user.NodeRid.ToHex() == request.SourceNodeRid
                   && user.NodeGeneration
                  == request.SourceNodeLifecycleGeneration;
    }

    private static bool RequiresRestore(
        ZLinkSpotNodeRuntime node,
        ZLinkSpotRetireStageRequest request)
    {
        var spotRelocations = request.InstanceSpot
            ? node.Registration.InstanceSpotRelocations
            : node.Registration.SpotRelocations;
        if (spotRelocations.TryGetValue(
                request.StableType,
                out var spot)
            && spot.PolicyKind == 2)
            return true;
        foreach (var actor in request.Actors)
            if (ZLinkActorAuthorityPayloadCodec.TryDecodeRelocating(
                    actor.AuthorityPayload,
                    out var authority)
                && node.Registration.ActorRelocations.TryGetValue(
                    authority.StableType,
                    out var relocation)
                && relocation.PolicyKind == 2)
                return true;
        return false;
    }

    private static bool ValidateActorDescriptors(
        ZLinkRelocationEnvelope envelope,
        ZLinkSpotRetireStageRequest request)
    {
        var participants = envelope.Participants
            .Where(static participant =>
                participant.ObjectKind == ZLinkPlacementObjectKind.Actor)
            .ToDictionary(
                static participant => participant.AuthorityKey,
                static participant => participant);
        if (participants.Count != request.Actors.Length)
            return false;
        var actorIds = new HashSet<string>(StringComparer.Ordinal);
        foreach (var descriptor in request.Actors)
        {
            if (!actorIds.Add(descriptor.ActorId)
                || !ZLinkActorAuthorityPayloadCodec.TryDecodeRelocating(
                    descriptor.AuthorityPayload,
                    out var actor)
                || actor.ActorId != descriptor.ActorId
                || actor.StableType != descriptor.StableType
                || !participants.ContainsKey(
                    ZLinkActorAuthorityPayloadCodec.AuthorityKey(
                        descriptor.ActorId)))
                return false;
        }
        return true;
    }

    internal async ValueTask<ZLinkSpotRetireReply> PublishInboundAsync(
        ZLinkSpotRetirePublishRequest request,
        RoutingId sourceNodeRid,
        CancellationToken cancellationToken)
    {
        await CleanupExpiredAsync().ConfigureAwait(false);
        var fence = new ZLinkAggregateFence(
            request.AggregateId,
            request.AggregateGeneration);
        if (!_staged.TryGetValue(fence, out var stage)
            || stage.SourceNodeRid != sourceNodeRid)
            return new ZLinkSpotRetireReply(false);
        if (Volatile.Read(ref stage.AuthorityPublished) != 0)
        {
            if (await IsAuthorityNormalizedAsync(
                        stage,
                        cancellationToken)
                    .ConfigureAwait(false)
                || await IsAuthorityPublishedAsync(
                        stage,
                        cancellationToken)
                    .ConfigureAwait(false))
            {
                await runtime.PrepareInboundSpotAggregateAsync(
                        stage,
                        cancellationToken)
                    .ConfigureAwait(false);
                return new ZLinkSpotRetireReply(true);
            }
            return new ZLinkSpotRetireReply(false);
        }
        if (!await IsAuthorityPublishedAsync(stage, cancellationToken)
                .ConfigureAwait(false))
            return new ZLinkSpotRetireReply(false);
        Volatile.Write(ref stage.AuthorityPublished, 1);
        await runtime.PrepareInboundSpotAggregateAsync(
                stage,
                cancellationToken)
            .ConfigureAwait(false);
        return new ZLinkSpotRetireReply(true);
    }

    private async ValueTask FinalizeStageAsync(
        TargetStage stage,
        CancellationToken cancellationToken)
    {
        if (!await IsAuthorityNormalizedAsync(stage, cancellationToken)
                .ConfigureAwait(false))
            await ValidateDurableCompletionRootAsync(
                    stage,
                    cancellationToken)
                .ConfigureAwait(false);
        await runtime.PublishInboundSpotAggregateAsync(
                stage,
                token => NormalizeAuthorityAsync(stage, token),
                cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask ValidateDurableCompletionRootAsync(
        TargetStage stage,
        CancellationToken cancellationToken)
    {
        var authorityStore = registration.Locations.ResolveStore()
                             ?? throw new ZLinkConfigurationException(
                                 "Location Store is not registered.");
        var relocationStore =
            registration.Locations.RelocationStoreInstance
            ?? throw new ZLinkConfigurationException(
                "Relocation Store is not registered.");
        var stagedSpot = stage.Envelope.Participants.Single(
            static participant => participant.ObjectKind
                is ZLinkPlacementObjectKind.UserSpot
                or ZLinkPlacementObjectKind.InstanceSpot);
        var read = await authorityStore.ReadAuthorityAsync(
                stagedSpot.AuthorityKey,
                cancellationToken)
            .ConfigureAwait(false);
        if (read is not ZLinkAuthorityReadResult.Found found
            || !ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                found.Snapshot.Payload.Span,
                out var publication)
            || publication.AggregateId != stage.Envelope.AggregateId)
            throw new ZLinkRelocationDataLostException(
                "Held ingress completion authority is unavailable.");
        var durable = await ZLinkRelocationTreeStore.GetAsync(
                relocationStore,
                publication.Reference,
                publication.ChecksumCrc32c,
                cancellationToken)
            .ConfigureAwait(false);
        var durableSpot = durable.Participants.Single(
            static participant => participant.ObjectKind
                is ZLinkPlacementObjectKind.UserSpot
                or ZLinkPlacementObjectKind.InstanceSpot);
        var expectedJobs = stagedSpot.AcceptedJobs
            .Concat(stage.HeldRecords)
            .OrderBy(static job => job.AcceptedSequence)
            .ToArray();
        if (!ZLinkSpotRetireCompletionMarker.IsCompleted(
                durableSpot.CompletionPayload.Span)
            || durable.AggregateId != publication.AggregateId
            || durable.AggregateGeneration
               != publication.AggregateGeneration
            || !durable.InventoryDigest.Span.SequenceEqual(
                publication.InventoryDigest.Span)
            || durable.Participants.Count != stage.Envelope.Participants.Count
            || !durable.Participants.Select(static item => item.AuthorityKey)
                .OrderBy(static key => key.Value, StringComparer.Ordinal)
                .SequenceEqual(
                    stage.Envelope.Participants
                        .Select(static item => item.AuthorityKey)
                        .OrderBy(static key => key.Value,
                            StringComparer.Ordinal))
            || durableSpot.AcceptedJobs.Count != expectedJobs.Length)
            throw new ZLinkRelocationDataLostException(
                "Held ingress completion root does not match target staging.");
        for (var index = 0; index < expectedJobs.Length; index++)
            if (durableSpot.AcceptedJobs[index].AcceptedSequence
                != expectedJobs[index].AcceptedSequence
                || !durableSpot.AcceptedJobs[index].Payload.Span.SequenceEqual(
                    expectedJobs[index].Payload.Span))
                throw new ZLinkRelocationDataLostException(
                    "Held ingress completion journal does not match target staging.");
    }

    internal async ValueTask<ZLinkSpotRetireReply> AbortInboundAsync(
        ZLinkSpotRetireAbortRequest request,
        RoutingId sourceNodeRid,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        await CleanupExpiredAsync().ConfigureAwait(false);
        var fence = new ZLinkAggregateFence(
            request.AggregateId,
            request.AggregateGeneration);
        if (_staged.TryGetValue(fence, out var candidate)
            && candidate.SourceNodeRid != sourceNodeRid)
            return new ZLinkSpotRetireReply(false);
        if (_staged.TryRemove(fence, out var stage))
        {
            await runtime.AbortInboundSpotAggregateAsync(stage)
                .ConfigureAwait(false);
            stage.ReleasePermit();
            ReleaseStageSlot();
        }
        return new ZLinkSpotRetireReply(true);
    }

    private async ValueTask<bool> IsAuthorityPublishedAsync(
        TargetStage stage,
        CancellationToken cancellationToken)
    {
        var authorityStore = registration.Locations.ResolveStore();
        var localOwner = runtime.LocationLifecycle?.OwnerToken;
        if (authorityStore is null || localOwner is not { } exactLocalOwner)
            return false;
        foreach (var participant in stage.Envelope.Participants)
        {
            var read = await authorityStore.ReadAuthorityAsync(
                    participant.AuthorityKey,
                    cancellationToken)
                .ConfigureAwait(false);
            if (read is not ZLinkAuthorityReadResult.Found found
                || found.Snapshot.ObjectGeneration
                   != participant.ObjectGeneration
                || found.Snapshot.AuthorityOwnerGeneration
                   != checked(participant.AuthorityOwnerGeneration + 1)
                || found.Snapshot.OwnerId != exactLocalOwner.OwnerId
                || found.Snapshot.OwnerLeaseGeneration
                   != exactLocalOwner.LeaseGeneration
                || !ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                    found.Snapshot.Payload.Span,
                    out var publication)
                || publication.Reference != stage.RelocationReference
                || publication.ChecksumCrc32c != stage.RelocationChecksum
                || publication.AggregateId != stage.Envelope.AggregateId
                || publication.AggregateGeneration
                   != stage.Envelope.AggregateGeneration
                || !publication.InventoryDigest.Span.SequenceEqual(
                    stage.Envelope.InventoryDigest.Span)
                || publication.TargetOwnerId != exactLocalOwner.OwnerId
                || publication.TargetOwnerLeaseGeneration
                   != exactLocalOwner.LeaseGeneration)
                return false;
        }
        return true;
    }

    private async ValueTask<bool> IsAuthorityNormalizedAsync(
        TargetStage stage,
        CancellationToken cancellationToken)
    {
        var authorityStore = registration.Locations.ResolveStore();
        var localOwner = runtime.LocationLifecycle?.OwnerToken;
        if (authorityStore is null || localOwner is not { } exactLocalOwner)
            return false;
        foreach (var participant in stage.Envelope.Participants)
        {
            var read = await authorityStore.ReadAuthorityAsync(
                    participant.AuthorityKey,
                    cancellationToken)
                .ConfigureAwait(false);
            if (read is not ZLinkAuthorityReadResult.Found found
                || found.Snapshot.ObjectGeneration
                   != participant.ObjectGeneration
                || found.Snapshot.AuthorityOwnerGeneration
                   != checked(participant.AuthorityOwnerGeneration + 1)
                || found.Snapshot.OwnerId != exactLocalOwner.OwnerId
                || found.Snapshot.OwnerLeaseGeneration
                   != exactLocalOwner.LeaseGeneration
                || ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                    found.Snapshot.Payload.Span,
                    out _))
                return false;
        }
        return true;
    }

    private async ValueTask NormalizeAuthorityAsync(
        TargetStage stage,
        CancellationToken cancellationToken)
    {
        if (await IsAuthorityNormalizedAsync(stage, cancellationToken)
                .ConfigureAwait(false))
            return;
        var authorityStore = registration.Locations.ResolveStore()
                             ?? throw new ZLinkConfigurationException(
                                 "Location Store is not registered.");
        var localOwner = runtime.LocationLifecycle?.OwnerToken
                         ?? throw new ZLinkConfigurationException(
                             "Location runtime is not registered.");
        var participants =
            new List<ZLinkAggregateRelocationParticipant>(
                stage.Envelope.Participants.Count);
        var stagedSpotForGeneration = stage.Envelope.Participants.Single(
            static participant => participant.ObjectKind
                is ZLinkPlacementObjectKind.UserSpot
                or ZLinkPlacementObjectKind.InstanceSpot);
        var expectedPublicationGeneration = checked(
            stage.Envelope.AggregateGeneration
            + (ZLinkSpotRetireCompletionMarker.IsCompleted(
                stagedSpotForGeneration.CompletionPayload.Span)
                ? 0UL
                : 1UL));
        string? publicationReference = null;
        uint publicationChecksum = 0;
        ReadOnlyMemory<byte> publicationInventoryDigest = default;
        foreach (var participant in stage.Envelope.Participants)
        {
            var read = await authorityStore.ReadAuthorityAsync(
                    participant.AuthorityKey,
                    cancellationToken)
                .ConfigureAwait(false);
            if (read is not ZLinkAuthorityReadResult.Found found
                || !ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                    found.Snapshot.Payload.Span,
                    out var publication)
                || publication.AggregateId != stage.Envelope.AggregateId
                || publication.AggregateGeneration
                   != expectedPublicationGeneration
                || publication.TargetOwnerId != localOwner.OwnerId
                || publication.TargetOwnerLeaseGeneration
                   != localOwner.LeaseGeneration)
                throw new ZLinkRelocationDataLostException(
                    $"Relocation authority '{participant.AuthorityKey.Value}' cannot be normalized.");
            if (publicationReference is null)
            {
                publicationReference = publication.Reference;
                publicationChecksum = publication.ChecksumCrc32c;
                publicationInventoryDigest = publication.InventoryDigest;
            }
            else if (publication.Reference != publicationReference
                     || publication.ChecksumCrc32c
                     != publicationChecksum
                     || !publication.InventoryDigest.Span.SequenceEqual(
                         publicationInventoryDigest.Span))
            {
                throw new ZLinkRelocationDataLostException(
                    "Relocation authorities disagree on the held ingress completion root.");
            }
            var ready = BuildTargetReadyPayload(
                participant.ObjectKind,
                publication.ApplicationPayload,
                stage,
                localOwner);
            participants.Add(new ZLinkAggregateRelocationParticipant(
                participant,
                found.Snapshot.StoreVersion,
                ZLinkAuthorityGenerationTransition.Preserve,
                ready,
                ReadOnlyMemory<byte>.Empty));
        }

        // A retry after process recovery must address the same normalization
        // transaction. Reusing the relocation id with the next generation
        // gives the authority Store an exact idempotency fence.
        var aggregateId = stage.Envelope.AggregateId;
        var spotParticipant = stage.Envelope.Participants.Single(
            static participant => participant.ObjectKind
                is ZLinkPlacementObjectKind.UserSpot
                or ZLinkPlacementObjectKind.InstanceSpot);
        var aggregateGeneration = checked(
            stage.Envelope.AggregateGeneration
            + (ZLinkSpotRetireCompletionMarker.IsCompleted(
                spotParticipant.CompletionPayload.Span)
                ? 1UL
                : 2UL));
        var targetStatus = stage.Node.Node.MeshStatus();
        var prepare = await authorityStore.PrepareAggregateAsync(
                new ZLinkAggregatePrepareRequest(
                    aggregateId,
                    aggregateGeneration,
                    participants.Select(static participant =>
                            new ZLinkAggregateParticipant(
                                participant.Envelope.AuthorityKey,
                                participant.ExpectedStoreVersion,
                                participant.OwnerTransition,
                                participant.ApplicationAuthorityPayload,
                                participant.MembershipMutation))
                        .ToArray(),
                    ZLinkAggregateInventoryDigest.Compute(participants),
                    new ZLinkMeshNodeDescriptorKey(
                        stage.SourceMeshName,
                        stage.Node.Node.RoutingId),
                    targetStatus.LifecycleGeneration,
                    new ZLinkCapacityVector(0, 0, null),
                    localOwner),
                cancellationToken)
            .ConfigureAwait(false);
        var fence = prepare switch
        {
            ZLinkAggregatePrepareResult.Prepared value => value.Fence,
            ZLinkAggregatePrepareResult.AlreadyPrepared value => value.Fence,
            _ => throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SpotMoving,
                $"SPOT '{stage.Spot.Activation.SpotId}' steady authority normalization conflicted.",
                true)
        };
        var commit = await authorityStore.CommitAggregateAsync(
                fence,
                cancellationToken)
            .ConfigureAwait(false);
        if (commit is not (
                ZLinkAggregateCommitResult.Committed
                or ZLinkAggregateCommitResult.AlreadyCommitted))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SpotMoving,
                $"SPOT '{stage.Spot.Activation.SpotId}' steady authority normalization failed.",
                true);
    }

    private static ReadOnlyMemory<byte> BuildTargetReadyPayload(
        ZLinkPlacementObjectKind kind,
        ReadOnlyMemory<byte> sourcePayload,
        TargetStage stage,
        ZLinkLocationOwnerToken targetOwner)
    {
        var targetNode = stage.Node.Node.RoutingId;
        var targetNodeGeneration =
            stage.Node.Node.MeshStatus().LifecycleGeneration;
        var ownerGeneration = checked((ulong)targetOwner.LeaseGeneration);
        if (kind == ZLinkPlacementObjectKind.UserSpot
            && ZLinkUserSpotAuthorityPayloadCodec.TryDecode(
                sourcePayload.Span,
                out var user))
            return ZLinkUserSpotAuthorityPayloadCodec.Encode(user with
            {
                State = ZLinkUserSpotAuthorityState.Ready,
                OwnerId = targetOwner.OwnerId,
                OwnerLeaseGeneration = ownerGeneration,
                NodeRid = targetNode,
                NodeGeneration = targetNodeGeneration
            });
        if (kind == ZLinkPlacementObjectKind.InstanceSpot
            && ZLinkInstanceSpotAuthorityPayloadCodec.TryDecode(
                sourcePayload.Span,
                out var instance))
            return ZLinkInstanceSpotAuthorityPayloadCodec.Encode(
                instance with
                {
                    State = ZLinkInstanceSpotAuthorityState.Ready,
                    OwnerId = targetOwner.OwnerId,
                    OwnerLeaseGeneration = ownerGeneration,
                    NodeRid = targetNode,
                    NodeGeneration = targetNodeGeneration
                });
        if (kind == ZLinkPlacementObjectKind.Actor
            && ZLinkActorRelocationAuthorityPayloadCodec.TryDecode(
                sourcePayload.Span,
                out var relocation)
            && relocation.RelocationId == stage.Envelope.AggregateId
            && relocation.Phase
               == ZLinkActorRelocationAuthorityPhase.Activated
            && ZLinkActorAuthorityPayloadCodec.TryDecodeRelocating(
                relocation.ApplicationPayload.Span,
                out var actor))
            return ZLinkActorRelocationAuthorityPayloadCodec.Encode(
                relocation with
                {
                    Phase = ZLinkActorRelocationAuthorityPhase.Steady,
                    ApplicationPayload =
                        ZLinkActorAuthorityPayloadCodec.Encode(actor with
                        {
                            State = ZLinkActorAuthorityState.Ready,
                            OwnerId = targetOwner.OwnerId,
                            OwnerLeaseGeneration = ownerGeneration,
                            NodeRid = targetNode,
                            NodeGeneration = targetNodeGeneration
                        })
                });
        throw new ZLinkRelocationDataLostException(
            $"Relocation participant kind '{kind}' has an invalid authority payload.");
    }

    private async ValueTask CleanupExpiredAsync()
    {
        var now = DateTimeOffset.UtcNow;
        foreach (var entry in _staged.Where(
                     entry => IsCleanupCandidate(entry.Value, now))
                 .ToArray())
            if (_staged.TryRemove(entry.Key, out var expired))
            {
                if (Volatile.Read(ref expired.AuthorityPublished) == 0)
                    await runtime.AbortInboundSpotAggregateAsync(expired)
                        .ConfigureAwait(false);
                expired.ReleasePermit();
                ReleaseStageSlot();
            }
    }

    private static bool IsCleanupCandidate(
        TargetStage stage,
        DateTimeOffset now) =>
        stage.ExpiresAt <= now
        && (Volatile.Read(ref stage.AuthorityPublished) == 0
            || Volatile.Read(ref stage.Published) != 0);

    private bool TryReserveStageSlot()
    {
        var count = Interlocked.Increment(ref _stagedCount);
        if (count <= MaxStagedAggregates)
            return true;
        Interlocked.Decrement(ref _stagedCount);
        return false;
    }

    private void ReleaseStageSlot()
    {
        if (Interlocked.Decrement(ref _stagedCount) < 0)
            throw new InvalidOperationException(
                "SPOT relocation stage accounting became negative.");
    }

    private static byte[] ComputeHeldDigest(
        IReadOnlyList<ZLinkSpotRetireHeldRecord> records)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        Span<byte> number = stackalloc byte[8];
        BinaryPrimitives.WriteUInt64BigEndian(
            number,
            checked((ulong)records.Count));
        hash.AppendData(number);
        foreach (var record in records)
        {
            BinaryPrimitives.WriteUInt64BigEndian(
                number,
                record.AcceptedSequence);
            hash.AppendData(number);
            BinaryPrimitives.WriteUInt64BigEndian(
                number,
                checked((ulong)record.Payload.LongLength));
            hash.AppendData(number);
            hash.AppendData(record.Payload);
        }
        return hash.GetHashAndReset();
    }
}

internal sealed record TargetStage(
    ZLinkSpotNodeRuntime Node,
    PreparedReservedSpot Spot,
    ZLinkRelocationEnvelope Envelope,
    IReadOnlyList<ZLinkActorRuntimeState> ActorStates,
    string StableType,
    string SourceMeshName,
    RoutingId SourceNodeRid,
    ulong SourceNodeLifecycleGeneration,
    ZLinkLocationOwnerToken SourceOwner,
    string RelocationReference,
    uint RelocationChecksum,
    DateTimeOffset ExpiresAt,
    byte[] StageRequestDigest,
    ZLinkSpotRelocationSeal TargetAdmissionSeal,
    IDisposable InboundPermit,
    ulong SourceAuthorityOwnerGeneration,
    ulong TargetAuthorityOwnerGeneration,
    string TargetMeshName,
    ulong TargetNodeLifecycleGeneration,
    ulong TargetOwnerLeaseGeneration)
{
    public int AuthorityPublished;
    public int Published;
    public int JobsStaged;
    public int ReplayedJobCount;
    public int LocalCatalogPublished;
    public SemaphoreSlim PublishGate { get; } = new(1, 1);
    public object HeldGate { get; } = new();
    public byte[]? HeldDigest;
    public ulong HeldHighWater;
    public IReadOnlyList<ZLinkRelocationQueuedJob> HeldRecords = [];
    private int _permitReleased;

    internal void ReleasePermit()
    {
        if (Interlocked.Exchange(ref _permitReleased, 1) == 0)
            InboundPermit.Dispose();
    }

    internal bool Matches(
        ZLinkSpotRetireStageRequest request,
        RoutingId sourceNodeRid) =>
        SourceNodeRid == sourceNodeRid
        && StageRequestDigest.AsSpan().SequenceEqual(
            ZLinkSpotRetireTargetRuntime.ComputeStageRequestDigest(request));
}
