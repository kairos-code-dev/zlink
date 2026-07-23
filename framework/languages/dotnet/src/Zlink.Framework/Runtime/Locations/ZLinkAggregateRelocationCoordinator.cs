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
    IReadOnlyList<ZLinkRelocationCapacityFence> TargetReservations,
    ZLinkLocationOwnerToken TargetOwner);

internal sealed record ZLinkAggregateRelocationPublished(
    ZLinkAggregateFence Fence,
    ZLinkRelocationStored Relocation,
    ZLinkRelocationEnvelope Envelope);

internal sealed class ZLinkAggregateRelocationCoordinator(
    IZLinkAuthorityStore authorityStore,
    IZLinkRelocationStore relocationStore)
{
    private static readonly TimeSpan Retention = TimeSpan.FromHours(24);

    internal async ValueTask<ZLinkAggregateRelocationPublished> PublishAsync(
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
        var root = ZLinkRelocationEnvelopeCodec.Encode(envelope);
        var stored = await relocationStore.PutRelocationAsync(
                root,
                Retention,
                cancellationToken)
            .ConfigureAwait(false);
        var fence = new ZLinkAggregateFence(
            request.AggregateId,
            request.AggregateGeneration);
        var prepared = false;
        try
        {
            ValidateStored(root, stored);
            var read = await relocationStore.GetRelocationAsync(
                    stored.Reference,
                    cancellationToken)
                .ConfigureAwait(false);
            if (read is not ZLinkRelocationReadResult.Found found
                || !found.Payload.Span.SequenceEqual(root))
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
                            checked((long)request.TargetOwner.Generation),
                            participant.ApplicationAuthorityPayload)),
                    participant.MembershipMutation))
                .ToArray();
            var prepare = await authorityStore.PrepareAggregateAsync(
                    new ZLinkAggregatePrepareRequest(
                        request.AggregateId,
                        request.AggregateGeneration,
                        publicationParticipants,
                        inventoryDigest,
                        request.TargetReservations,
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

            var commit = await authorityStore.CommitAggregateAsync(
                    fence,
                    cancellationToken)
                .ConfigureAwait(false);
            if (commit is not (ZLinkAggregateCommitResult.Committed
                or ZLinkAggregateCommitResult.AlreadyCommitted))
                throw new InvalidOperationException(
                    $"The aggregate relocation commit failed with '{commit}'.");
            return new ZLinkAggregateRelocationPublished(
                fence,
                stored,
                envelope);
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
                return new ZLinkAggregateRelocationPublished(
                    fence,
                    stored,
                    envelope);

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
            await relocationStore.DeleteRelocationAsync(
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
        if (request.TargetOwner.Generation is 0 or > long.MaxValue)
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

    private static void ValidateStored(
        ReadOnlyMemory<byte> root,
        ZLinkRelocationStored stored)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(stored.Reference);
        if (stored.ChecksumCrc32c != ZLinkCrc32C.Compute(root.Span))
            throw new ZLinkRelocationDataLostException(
                "Relocation Store returned an invalid aggregate checksum.");
        if (stored.ExpiresAt <= stored.StoreNow)
            throw new InvalidDataException(
                "Relocation Store returned a non-positive retention interval.");
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
