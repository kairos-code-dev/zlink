namespace Zlink.Framework.Runtime.Locations;

internal sealed record ZLinkRelocationRecoveryCandidate(
    ZLinkRelocationManifestReference Reference,
    ZLinkRelocationEnvelope Envelope,
    IReadOnlyList<ZLinkAuthorityEntry> Authorities);

/// <summary>
/// Finds durable Actor, User Spot and Instance Spot relocations whose authority
/// already publishes an immutable root. Entry Spots are intentionally excluded.
/// A root is offered once per scan even when a Spot aggregate has several
/// authority rows. The resume callback owns
/// target materialization, replay, source cleanup, and steady normalization;
/// it must be idempotent for the aggregate identity.
/// </summary>
internal sealed class ZLinkRelocationStartupRecovery(
    IZLinkAuthorityStore authorityStore,
    IZLinkRelocationStore relocationStore)
{
    private const int PageSize = 128;
    private static readonly string[] Prefixes = ["zla1:a:", "zla1:s:"];

    internal async ValueTask RecoverAsync(
        Func<
            ZLinkRelocationRecoveryCandidate,
            CancellationToken,
            ValueTask> resume,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(resume);
        var linked = new Dictionary<
            string,
            RecoveryGroup>(StringComparer.Ordinal);
        foreach (var prefix in Prefixes)
            await ScanPrefixAsync(prefix, linked, cancellationToken)
                .ConfigureAwait(false);

        var reader = new ZLinkRelocationPublicationCoordinator(
            authorityStore,
            relocationStore);
        foreach (var group in linked.Values
                     .OrderBy(static value => value.Reference.Reference,
                         StringComparer.Ordinal))
        {
            cancellationToken.ThrowIfCancellationRequested();
            ZLinkRelocationEnvelope envelope;
            try
            {
                envelope = await reader.ReadPreparedAsync(
                        group.Reference,
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
            ValidateLinkedAuthorities(group.Authorities, envelope);
            await resume(
                    new ZLinkRelocationRecoveryCandidate(
                        group.Reference,
                        envelope,
                        group.Authorities
                            .OrderBy(static entry => entry.Key.Value,
                                StringComparer.Ordinal)
                            .ToArray()),
                    cancellationToken)
                .ConfigureAwait(false);
        }
    }

    internal async ValueTask<ZLinkRelocationRecoveryCandidate?>
        TryReadExactPublishedAsync(
            ZLinkRelocationEnvelope staging,
            CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(staging);
        var linked = new Dictionary<string, RecoveryGroup>(
            StringComparer.Ordinal);
        var unpublished = 0;
        foreach (var participant in staging.Participants)
        {
            var read = await authorityStore.ReadAuthorityAsync(
                    participant.AuthorityKey,
                    cancellationToken)
                .ConfigureAwait(false);
            if (read is not ZLinkAuthorityReadResult.Found found)
                throw DataLost(
                    $"Relocation authority '{participant.AuthorityKey.Value}' is unavailable during exact reconciliation.");
            var entry = new ZLinkAuthorityEntry(
                participant.AuthorityKey,
                found.Snapshot);
            if (!ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                    found.Snapshot.Payload.Span,
                    out _))
            {
                unpublished++;
                continue;
            }
            AddPublished(entry, linked);
        }

        if (unpublished == staging.Participants.Count)
            return null;
        if (unpublished != 0 || linked.Count != 1)
            throw DataLost(
                $"Relocation aggregate '{staging.AggregateId:N}' has a partially visible publication.");

        var group = linked.Values.Single();
        ZLinkRelocationEnvelope envelope;
        try
        {
            envelope = await new ZLinkRelocationPublicationCoordinator(
                    authorityStore,
                    relocationStore)
                .ReadPreparedAsync(group.Reference, cancellationToken)
                .ConfigureAwait(false);
        }
        catch (ZLinkRelocationDataLostException error)
        {
            throw DataLost(error.Message, error);
        }
        ValidateLinkedAuthorities(group.Authorities, envelope);
        return new ZLinkRelocationRecoveryCandidate(
            group.Reference,
            envelope,
            group.Authorities
                .OrderBy(static entry => entry.Key.Value,
                    StringComparer.Ordinal)
                .ToArray());
    }

    private async ValueTask ScanPrefixAsync(
        string prefix,
        Dictionary<string, RecoveryGroup> linked,
        CancellationToken cancellationToken)
    {
        ZLinkAuthorityScanCursor? cursor = null;
        while (true)
        {
            var scan = await authorityStore.ListAuthoritiesAsync(
                    prefix,
                    cursor,
                    PageSize,
                    cancellationToken)
                .ConfigureAwait(false);
            if (scan is ZLinkAuthorityScanResult.ScanExpired)
            {
                RemovePrefix(linked, prefix);
                cursor = null;
                continue;
            }

            var page = ((ZLinkAuthorityScanResult.Page)scan).Value;
            foreach (var entry in page.Items)
                AddPublished(entry, linked);
            cursor = page.NextCursor;
            if (cursor is null) return;
        }
    }

    private static void AddPublished(
        ZLinkAuthorityEntry entry,
        Dictionary<string, RecoveryGroup> linked)
    {
        if (entry.Snapshot.Allocation.ObjectKind is not (
                ZLinkPlacementObjectKind.Actor
                or ZLinkPlacementObjectKind.UserSpot
                or ZLinkPlacementObjectKind.InstanceSpot)
            || !ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                entry.Snapshot.Payload.Span,
                out var publication))
            return;
        if (!string.Equals(
                publication.TargetOwnerId,
                entry.Snapshot.OwnerId,
                StringComparison.Ordinal)
            || publication.TargetOwnerLeaseGeneration
            != entry.Snapshot.OwnerLeaseGeneration)
            throw DataLost(
                $"Authority '{entry.Key.Value}' does not match its published relocation owner fence.");

        var reference = new ZLinkRelocationManifestReference(
            publication.Reference,
            publication.ChecksumCrc32c,
            publication.AggregateId,
            publication.AggregateGeneration,
            publication.InventoryDigest);
        if (!linked.TryGetValue(reference.Reference, out var group))
        {
            linked.Add(
                reference.Reference,
                new RecoveryGroup(reference, [entry]));
            return;
        }
        if (group.Reference.ChecksumCrc32c != reference.ChecksumCrc32c
            || group.Reference.AggregateId != reference.AggregateId
            || group.Reference.AggregateGeneration
            != reference.AggregateGeneration
            || !group.Reference.InventoryDigest.Span.SequenceEqual(
                reference.InventoryDigest.Span))
            throw DataLost(
                $"Relocation reference '{reference.Reference}' has inconsistent authority manifests.");
        group.Authorities.Add(entry);
    }

    private static void ValidateLinkedAuthorities(
        IReadOnlyList<ZLinkAuthorityEntry> authorities,
        ZLinkRelocationEnvelope envelope)
    {
        if (!envelope.CanonicalLogicalStream.IsEmpty)
        {
            if (envelope.Participants.Count != authorities.Count
                || authorities.Count(static authority =>
                    authority.Snapshot.Allocation.ObjectKind
                    is ZLinkPlacementObjectKind.UserSpot
                    or ZLinkPlacementObjectKind.InstanceSpot) != 1
                || authorities.Any(static authority =>
                    authority.Snapshot.Allocation.ObjectKind
                    is not (ZLinkPlacementObjectKind.Actor
                    or ZLinkPlacementObjectKind.UserSpot
                    or ZLinkPlacementObjectKind.InstanceSpot)))
                throw DataLost(
                    $"Canonical relocation aggregate '{envelope.AggregateId:N}' authority inventory is invalid.");
            return;
        }
        var participants = envelope.Participants.ToDictionary(
            static participant => participant.AuthorityKey.Value,
            StringComparer.Ordinal);
        if (participants.Count != authorities.Count)
            throw DataLost(
                $"Relocation aggregate '{envelope.AggregateId:N}' does not have one published authority per participant.");
        foreach (var authority in authorities)
        {
            if (!participants.TryGetValue(
                    authority.Key.Value,
                    out var participant)
                || participant.ObjectKind
                != authority.Snapshot.Allocation.ObjectKind
                || participant.ObjectGeneration
                != authority.Snapshot.ObjectGeneration)
                throw DataLost(
                    $"Relocation root does not contain exact authority participant '{authority.Key.Value}'.");
        }
    }

    private static void RemovePrefix(
        Dictionary<string, RecoveryGroup> linked,
        string prefix)
    {
        foreach (var (reference, group) in linked.ToArray())
        {
            group.Authorities.RemoveAll(
                entry => entry.Key.Value.StartsWith(
                    prefix,
                    StringComparison.Ordinal));
            if (group.Authorities.Count == 0)
                linked.Remove(reference);
        }
    }

    private static ZLinkFrameworkException DataLost(
        string message,
        Exception? innerException = null) =>
        new(
            ZLinkFrameworkErrorKind.RelocationDataLost,
            message,
            isRetriable: false,
            innerException);

    private sealed record RecoveryGroup(
        ZLinkRelocationManifestReference Reference,
        List<ZLinkAuthorityEntry> Authorities);
}
