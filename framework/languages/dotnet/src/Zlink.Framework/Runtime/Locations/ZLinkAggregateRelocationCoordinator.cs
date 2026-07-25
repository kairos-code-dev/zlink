using System.Security.Cryptography;
using System.Text;

namespace Zlink.Framework.Runtime.Locations;

internal sealed record ZLinkAggregateRelocationParticipant(
    ZLinkRelocationParticipantEnvelope Envelope,
    string ExpectedStoreVersion,
    ZLinkAuthorityGenerationTransition OwnerTransition,
    ReadOnlyMemory<byte> ApplicationAuthorityPayload,
    ReadOnlyMemory<byte> MembershipMutation);

internal sealed record ZLinkAggregateRelocationRequest(
    Guid AggregateId,
    ulong AggregateGeneration,
    IReadOnlyList<ZLinkAggregateRelocationParticipant> Participants,
    ZLinkMeshNodeDescriptorKey TargetDescriptor,
    ulong TargetDescriptorLifecycleGeneration,
    ZLinkCapacityVector Capacity,
    ZLinkLocationOwnerToken TargetOwner);

internal sealed record ZLinkAggregateRelocationPublished(
    ZLinkAggregateFence Fence,
    ZLinkRelocationStored Relocation,
    ZLinkRelocationEnvelope Envelope);

internal sealed record ZLinkPreparedAggregateRelocation(
    ZLinkAggregateFence Fence,
    ZLinkRelocationStored Relocation,
    ZLinkRelocationEnvelope Envelope,
    IReadOnlyList<ZLinkAggregateRelocationParticipant> Participants,
    ReadOnlyMemory<byte> InventoryDigest);

internal sealed class ZLinkAggregateRelocationCoordinator(
    IZLinkAuthorityStore authorityStore,
    IZLinkRelocationStore relocationStore)
{
    private static readonly TimeSpan Retention = TimeSpan.FromHours(24);

    internal async ValueTask<ZLinkAggregateRelocationPublished> PublishAsync(
        ZLinkAggregateRelocationRequest request,
        CancellationToken cancellationToken = default)
    {
        var prepared = await PrepareAsync(request, cancellationToken)
            .ConfigureAwait(false);
        return await CommitAsync(prepared, cancellationToken)
            .ConfigureAwait(false);
    }

    /// <summary>
    /// Stores and verifies the immutable root, then reserves the exact
    /// authority aggregate without publishing it. The target must finish
    /// factory and Restore staging before calling <see cref="CommitAsync"/>.
    /// </summary>
    internal async ValueTask<ZLinkPreparedAggregateRelocation> PrepareAsync(
        ZLinkAggregateRelocationRequest request,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        Validate(request);
        cancellationToken.ThrowIfCancellationRequested();

        var inventoryDigest = ZLinkAggregateInventoryDigest.Compute(
            request.Participants);
        var envelope = new ZLinkRelocationEnvelope(
            request.AggregateId,
            request.AggregateGeneration,
            inventoryDigest,
            request.Participants
                .Select(static participant => participant.Envelope)
                .ToArray());
        var tree = await ZLinkRelocationTreeStore.PutAsync(
                relocationStore,
                envelope,
                Retention,
                cancellationToken)
            .ConfigureAwait(false);
        var stored = tree.Root;
        var fence = new ZLinkAggregateFence(
            request.AggregateId,
            request.AggregateGeneration);
        var prepared = false;
        try
        {
            var restored = await ZLinkRelocationTreeStore.GetAsync(
                    relocationStore,
                    stored.Reference,
                    stored.ChecksumCrc32c,
                    cancellationToken)
                .ConfigureAwait(false);
            if (restored.AggregateId != envelope.AggregateId
                || restored.AggregateGeneration
                   != envelope.AggregateGeneration
                || !restored.InventoryDigest.Span.SequenceEqual(
                    inventoryDigest.AsSpan()))
                throw new ZLinkRelocationDataLostException(
                    "Relocation Store did not preserve the aggregate root.");

            var publicationParticipants = request.Participants
                .Select(participant => new ZLinkAggregateParticipant(
                    participant.Envelope.AuthorityKey,
                    participant.ExpectedStoreVersion,
                    participant.OwnerTransition,
                    ZLinkRelocationAuthorityPayloadCodec.Encode(
                        new ZLinkRelocationAuthorityPayload(
                            stored.Reference,
                            stored.ChecksumCrc32c,
                            request.AggregateId,
                            request.AggregateGeneration,
                            inventoryDigest,
                            request.TargetOwner.OwnerId,
                            request.TargetOwner.LeaseGeneration,
                            participant.ApplicationAuthorityPayload)),
                    participant.MembershipMutation))
                .ToArray();
            var prepare = await authorityStore.PrepareAggregateAsync(
                    new ZLinkAggregatePrepareRequest(
                        request.AggregateId,
                        request.AggregateGeneration,
                        publicationParticipants,
                        inventoryDigest,
                        request.TargetDescriptor,
                        request.TargetDescriptorLifecycleGeneration,
                        request.Capacity,
                        request.TargetOwner),
                    cancellationToken)
                .ConfigureAwait(false);
            switch (prepare)
            {
                case ZLinkAggregatePrepareResult.Prepared value:
                    fence = value.Fence;
                    prepared = true;
                    break;
                case ZLinkAggregatePrepareResult.AlreadyPrepared value:
                    fence = value.Fence;
                    prepared = true;
                    break;
                case ZLinkAggregatePrepareResult.GenerationExhausted:
                    throw new InvalidOperationException(
                        "The aggregate generation space was exhausted.");
                default:
                    throw new InvalidOperationException(
                        "The aggregate relocation prepare was rejected.");
            }

            return new ZLinkPreparedAggregateRelocation(
                fence,
                stored,
                envelope,
                request.Participants,
                inventoryDigest);
        }
        catch
        {
            var published = await IsPublishedAsync(
                    request.Participants,
                    stored,
                    request.AggregateId,
                    request.AggregateGeneration,
                    inventoryDigest)
                .ConfigureAwait(false);
            if (published)
                return new ZLinkPreparedAggregateRelocation(
                    fence,
                    stored,
                    envelope,
                    request.Participants,
                    inventoryDigest);

            var safeToDelete = !prepared;
            if (prepared)
            {
                try
                {
                    var abort = await authorityStore.AbortAggregateAsync(
                            fence,
                            CancellationToken.None)
                        .ConfigureAwait(false);
                    safeToDelete = abort is ZLinkAggregateAbortResult.Aborted
                        or ZLinkAggregateAbortResult.AlreadyAborted;
                }
                catch
                {
                    safeToDelete = false;
                }
            }
            if (safeToDelete)
                await DeleteOrphanAsync(stored.Reference).ConfigureAwait(false);
            throw;
        }
    }

    internal async ValueTask<ZLinkAggregateRelocationPublished> CommitAsync(
        ZLinkPreparedAggregateRelocation prepared,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(prepared);
        cancellationToken.ThrowIfCancellationRequested();
        try
        {
            await ZLinkRelocationTreeStore.RenewTreeAsync(
                    relocationStore,
                    prepared.Relocation.Reference,
                    prepared.Relocation.ChecksumCrc32c,
                    Retention,
                    cancellationToken)
                .ConfigureAwait(false);
            var commit = await authorityStore.CommitAggregateAsync(
                    prepared.Fence,
                    cancellationToken)
                .ConfigureAwait(false);
            if (commit is not (ZLinkAggregateCommitResult.Committed
                or ZLinkAggregateCommitResult.AlreadyCommitted))
                throw new InvalidOperationException(
                    $"The aggregate relocation commit failed with '{commit}'.");
        }
        catch
        {
            if (!await IsPublishedAsync(
                    prepared.Participants,
                    prepared.Relocation,
                    prepared.Envelope.AggregateId,
                    prepared.Envelope.AggregateGeneration,
                    prepared.InventoryDigest)
                    .ConfigureAwait(false))
                throw;
        }

        return new ZLinkAggregateRelocationPublished(
            prepared.Fence,
            prepared.Relocation,
            prepared.Envelope);
    }

    internal async ValueTask CompleteSourceCleanupAsync(
        ZLinkAggregateRelocationPublished published,
        ZLinkMeshNodeDescriptorKey targetDescriptor,
        ulong targetDescriptorLifecycleGeneration,
        ZLinkLocationOwnerToken targetOwner,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(published);
        var spot = published.Envelope.Participants.Single(
            static participant => participant.ObjectKind
                is ZLinkPlacementObjectKind.UserSpot
                or ZLinkPlacementObjectKind.InstanceSpot);
        if (!ZLinkSpotRetireCompletionMarker.IsPending(
                spot.CompletionPayload.Span))
            throw new InvalidOperationException(
                "The initial relocation root is not source-cleanup pending.");

        var completedEnvelope = published.Envelope with
        {
            AggregateGeneration = checked(
                published.Envelope.AggregateGeneration + 1),
            Participants = published.Envelope.Participants.Select(
                    participant => participant.AuthorityKey
                                   == spot.AuthorityKey
                        ? participant with
                        {
                            CompletionPayload =
                                ZLinkSpotRetireCompletionMarker
                                    .CreateCompleted()
                        }
                        : participant)
                .ToArray()
        };
        var completedTree = await ZLinkRelocationTreeStore.PutAsync(
                relocationStore,
                completedEnvelope,
                Retention,
                cancellationToken)
            .ConfigureAwait(false);
        var completed = completedTree.Root;
        var mutations = new List<ZLinkAggregateParticipant>(
            completedEnvelope.Participants.Count);
        var alreadyCompleted = true;
        foreach (var participant in completedEnvelope.Participants)
        {
            var read = await authorityStore.ReadAuthorityAsync(
                    participant.AuthorityKey,
                    cancellationToken)
                .ConfigureAwait(false);
            if (read is not ZLinkAuthorityReadResult.Found found)
                throw new ZLinkRelocationDataLostException(
                    $"Relocation authority '{participant.AuthorityKey.Value}' disappeared before source cleanup completion.");
            if (!ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                    found.Snapshot.Payload.Span,
                    out var current))
            {
                // A retry after target ACK can observe the steady payload.
                continue;
            }
            if (current.AggregateId != completedEnvelope.AggregateId
                || current.TargetOwnerId != targetOwner.OwnerId
                || current.TargetOwnerLeaseGeneration
                   != targetOwner.LeaseGeneration)
                throw new ZLinkRelocationDataLostException(
                    $"Relocation authority '{participant.AuthorityKey.Value}' has a different completion fence.");
            if (current.AggregateGeneration
                    == completedEnvelope.AggregateGeneration
                && current.Reference == completed.Reference
                && current.ChecksumCrc32c == completed.ChecksumCrc32c)
                continue;
            alreadyCompleted = false;
            if (current.AggregateGeneration
                    != published.Envelope.AggregateGeneration
                || current.Reference != published.Relocation.Reference
                || current.ChecksumCrc32c
                   != published.Relocation.ChecksumCrc32c)
                throw new ZLinkRelocationDataLostException(
                    $"Relocation authority '{participant.AuthorityKey.Value}' is not source-cleanup pending.");
            mutations.Add(new ZLinkAggregateParticipant(
                participant.AuthorityKey,
                found.Snapshot.StoreVersion,
                ZLinkAuthorityGenerationTransition.Preserve,
                ZLinkRelocationAuthorityPayloadCodec.Encode(
                    current with
                    {
                        Reference = completed.Reference,
                        ChecksumCrc32c = completed.ChecksumCrc32c,
                        AggregateGeneration =
                            completedEnvelope.AggregateGeneration
                    }),
                ReadOnlyMemory<byte>.Empty));
        }
        if (alreadyCompleted || mutations.Count == 0)
            return;
        if (mutations.Count != completedEnvelope.Participants.Count)
            throw new ZLinkRelocationDataLostException(
                "Relocation source-cleanup completion is partially visible.");

        await ZLinkRelocationTreeStore.RenewTreeAsync(
                relocationStore,
                completed.Reference,
                completed.ChecksumCrc32c,
                Retention,
                cancellationToken)
            .ConfigureAwait(false);
        var prepare = await authorityStore.PrepareAggregateAsync(
                new ZLinkAggregatePrepareRequest(
                    completedEnvelope.AggregateId,
                    completedEnvelope.AggregateGeneration,
                    mutations,
                    completedEnvelope.InventoryDigest,
                    targetDescriptor,
                    targetDescriptorLifecycleGeneration,
                    new ZLinkCapacityVector(0, 0, null),
                    targetOwner),
                cancellationToken)
            .ConfigureAwait(false);
        var fence = prepare switch
        {
            ZLinkAggregatePrepareResult.Prepared value => value.Fence,
            ZLinkAggregatePrepareResult.AlreadyPrepared value => value.Fence,
            _ => throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SpotMoving,
                "Source-cleanup completion authority prepare conflicted.",
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
                "Source-cleanup completion authority commit failed.",
                true);
        await DeleteOrphanAsync(published.Relocation.Reference)
            .ConfigureAwait(false);
    }

    internal async ValueTask AbortAsync(
        ZLinkPreparedAggregateRelocation prepared)
    {
        ArgumentNullException.ThrowIfNull(prepared);
        var abort = await authorityStore.AbortAggregateAsync(
                prepared.Fence,
                CancellationToken.None)
            .ConfigureAwait(false);
        if (abort is not (ZLinkAggregateAbortResult.Aborted
            or ZLinkAggregateAbortResult.AlreadyAborted))
            throw new InvalidOperationException(
                $"The aggregate relocation abort failed with '{abort}'.");
        await DeleteOrphanAsync(prepared.Relocation.Reference)
            .ConfigureAwait(false);
    }

    private async ValueTask<bool> IsPublishedAsync(
        IReadOnlyList<ZLinkAggregateRelocationParticipant> participants,
        ZLinkRelocationStored stored,
        Guid aggregateId,
        ulong aggregateGeneration,
        ReadOnlyMemory<byte> inventoryDigest)
    {
        foreach (var participant in participants)
        {
            ZLinkAuthorityReadResult read;
            try
            {
                read = await authorityStore.ReadAuthorityAsync(
                        participant.Envelope.AuthorityKey,
                        CancellationToken.None)
                    .ConfigureAwait(false);
            }
            catch
            {
                return false;
            }
            if (read is not ZLinkAuthorityReadResult.Found found
                || !ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                    found.Snapshot.Payload.Span,
                    out var publication)
                || !string.Equals(
                    publication.Reference,
                    stored.Reference,
                    StringComparison.Ordinal)
                || publication.ChecksumCrc32c != stored.ChecksumCrc32c
                || publication.AggregateId != aggregateId
                || publication.AggregateGeneration != aggregateGeneration
                || !publication.InventoryDigest.Span.SequenceEqual(
                    inventoryDigest.Span))
                return false;
        }
        return true;
    }

    private async ValueTask DeleteOrphanAsync(string reference)
    {
        try
        {
            await ZLinkRelocationTreeStore.DeleteTreeAsync(
                    relocationStore,
                    reference,
                    CancellationToken.None)
                .ConfigureAwait(false);
        }
        catch
        {
        }
    }

    private static void Validate(ZLinkAggregateRelocationRequest request)
    {
        if (request.AggregateId == Guid.Empty)
            throw new ArgumentException(
                "The aggregate id must not be empty.",
                nameof(request));
        if (request.AggregateGeneration is 0 or > long.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(request));
        if (request.TargetOwner.LeaseGeneration <= 0)
            throw new ArgumentOutOfRangeException(nameof(request));
        if (request.Participants.Count is < 1 or > 1024)
            throw new ArgumentOutOfRangeException(nameof(request));
        var keys = new HashSet<string>(StringComparer.Ordinal);
        foreach (var participant in request.Participants)
        {
            ArgumentException.ThrowIfNullOrWhiteSpace(
                participant.ExpectedStoreVersion);
            if (!keys.Add(participant.Envelope.AuthorityKey.Value))
                throw new ArgumentException(
                    "Aggregate participant keys must be unique.",
                    nameof(request));
        }
    }

}

internal static class ZLinkAggregateInventoryDigest
{
    internal static byte[] Compute(
        IReadOnlyList<ZLinkAggregateRelocationParticipant> participants)
    {
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream, Encoding.UTF8, leaveOpen: true);
        foreach (var participant in participants.OrderBy(
                     static value => value.Envelope.AuthorityKey.Value,
                     StringComparer.Ordinal))
        {
            WriteString(writer, participant.Envelope.AuthorityKey.Value);
            writer.Write((byte)participant.Envelope.ObjectKind);
            writer.Write(participant.Envelope.ObjectGeneration);
            writer.Write(participant.Envelope.AuthorityOwnerGeneration);
            WriteString(writer, participant.ExpectedStoreVersion);
            writer.Write((byte)participant.OwnerTransition);
            WriteBytes(writer, participant.ApplicationAuthorityPayload.Span);
            WriteBytes(writer, participant.MembershipMutation.Span);
        }
        writer.Flush();
        return SHA256.HashData(stream.GetBuffer().AsSpan(0, checked((int)stream.Length)));
    }

    private static void WriteString(BinaryWriter writer, string value)
    {
        var encoded = Encoding.UTF8.GetBytes(value);
        writer.Write(encoded.Length);
        writer.Write(encoded);
    }

    private static void WriteBytes(BinaryWriter writer, ReadOnlySpan<byte> value)
    {
        writer.Write(value.Length);
        writer.Write(value);
    }
}
