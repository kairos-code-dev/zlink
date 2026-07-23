using System.Text;

namespace Zlink.Framework.Runtime.Locations;

internal sealed record ZLinkRelocationPublicationRequest(
    ZLinkAuthorityKey AuthorityKey,
    ZLinkAuthorityExpectation Expectation,
    ZLinkAuthorityGenerationTransition GenerationTransition,
    string TargetOwnerId,
    long TargetOwnerLeaseGeneration,
    ReadOnlyMemory<byte> ApplicationAuthorityPayload,
    ZLinkRelocationCapacityFence? RelocationCapacityFence,
    ZLinkRelocationEnvelope Envelope);

internal sealed record ZLinkPublishedRelocation(
    ZLinkAuthoritySnapshot Authority,
    ZLinkRelocationStored Relocation,
    ZLinkRelocationEnvelope Envelope);

internal sealed class ZLinkRelocationDataLostException(string message)
    : IOException(message);

internal sealed class ZLinkRelocationPublicationConflictException(
    ZLinkAuthorityReadResult current)
    : InvalidOperationException("The relocation authority publication conflicted.")
{
    internal ZLinkAuthorityReadResult Current { get; } = current;
}

internal sealed class ZLinkRelocationPublicationCoordinator(
    IZLinkAuthorityStore authorityStore,
    IZLinkRelocationStore relocationStore)
{
    private static readonly TimeSpan Retention = TimeSpan.FromHours(24);

    internal async ValueTask<ZLinkPublishedRelocation> PublishAsync(
        ZLinkRelocationPublicationRequest request,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(request);
        ValidateRequest(request);
        cancellationToken.ThrowIfCancellationRequested();

        var root = ZLinkRelocationEnvelopeCodec.Encode(request.Envelope);
        var stored = await relocationStore.PutRelocationAsync(
                root,
                Retention,
                cancellationToken)
            .ConfigureAwait(false);
        byte[] publishedPayload;
        try
        {
            ValidateStored(root, stored);
            publishedPayload = ZLinkRelocationAuthorityPayloadCodec.Encode(
                new ZLinkRelocationAuthorityPayload(
                    stored.Reference,
                    stored.ChecksumCrc32c,
                    request.Envelope.AggregateId,
                    request.Envelope.AggregateGeneration,
                    request.Envelope.InventoryDigest,
                    request.TargetOwnerId,
                    request.TargetOwnerLeaseGeneration,
                    request.ApplicationAuthorityPayload));
        }
        catch
        {
            await DeleteOrphanAsync(stored.Reference).ConfigureAwait(false);
            throw;
        }

        try
        {
            await VerifyStoredRootAsync(stored, root, cancellationToken)
                .ConfigureAwait(false);
            var result = await authorityStore.CompareExchangeAuthorityAsync(
                    request.AuthorityKey,
                    request.Expectation,
                    new ZLinkAuthorityMutation.Put(
                        publishedPayload,
                        request.GenerationTransition,
                        request.GenerationTransition
                        == ZLinkAuthorityGenerationTransition.Preserve
                            ? null
                            : new ZLinkLocationOwnerToken(
                                request.TargetOwnerId,
                                request.TargetOwnerLeaseGeneration),
                        request.RelocationCapacityFence),
                    cancellationToken)
                .ConfigureAwait(false);
            switch (result)
            {
                case ZLinkAuthorityCompareExchangeResult.Stored success:
                    ValidatePublishedSnapshot(success.Snapshot, request, stored);
                    return new ZLinkPublishedRelocation(
                        success.Snapshot,
                        stored,
                        request.Envelope);

                case ZLinkAuthorityCompareExchangeResult.Conflict conflict:
                    await DeleteOrphanAsync(stored.Reference).ConfigureAwait(false);
                    throw new ZLinkRelocationPublicationConflictException(
                        conflict.Current);

                case ZLinkAuthorityCompareExchangeResult.GenerationExhausted:
                    await DeleteOrphanAsync(stored.Reference).ConfigureAwait(false);
                    throw new InvalidOperationException(
                        "The authority generation space was exhausted.");

                default:
                    await DeleteOrphanAsync(stored.Reference).ConfigureAwait(false);
                    throw new InvalidOperationException(
                        "The authority store returned an invalid relocation publication result.");
            }
        }
        catch (ZLinkRelocationPublicationConflictException)
        {
            throw;
        }
        catch
        {
            // A provider exception or waiter cancellation can happen after the
            // CAS committed. Reconcile against the authority before deleting the
            // immutable root, because deleting a published root is data loss.
            var current = await TryReadAuthorityWithoutCancellationAsync(
                    request.AuthorityKey)
                .ConfigureAwait(false);
            if (current is ZLinkAuthorityReadResult.Found found
                && ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                    found.Snapshot.Payload.Span,
                    out var publication)
                && string.Equals(
                    publication.Reference,
                    stored.Reference,
                    StringComparison.Ordinal)
                && publication.ChecksumCrc32c == stored.ChecksumCrc32c)
            {
                ValidatePublishedSnapshot(
                    found.Snapshot,
                    request,
                    stored);
                return new ZLinkPublishedRelocation(
                    found.Snapshot,
                    stored,
                    request.Envelope);
            }

            await DeleteOrphanAsync(stored.Reference).ConfigureAwait(false);
            throw;
        }
    }

    internal async ValueTask<ZLinkPublishedRelocation?> RecoverAsync(
        ZLinkAuthorityKey key,
        CancellationToken cancellationToken = default)
    {
        var read = await authorityStore.ReadAuthorityAsync(key, cancellationToken)
            .ConfigureAwait(false);
        if (read is ZLinkAuthorityReadResult.Missing)
            return null;
        var authority = ((ZLinkAuthorityReadResult.Found)read).Snapshot;
        if (!ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                authority.Payload.Span,
                out var publication))
            return null;

        var readRoot = await relocationStore.GetRelocationAsync(
                publication.Reference,
                cancellationToken)
            .ConfigureAwait(false);
        if (readRoot is ZLinkRelocationReadResult.Missing)
            throw new ZLinkRelocationDataLostException(
                $"Published relocation root '{publication.Reference}' is missing.");
        var root = ((ZLinkRelocationReadResult.Found)readRoot).Payload;
        if (ZLinkCrc32C.Compute(root.Span) != publication.ChecksumCrc32c)
            throw new ZLinkRelocationDataLostException(
                $"Published relocation root '{publication.Reference}' failed its checksum.");

        ZLinkRelocationEnvelope envelope;
        try
        {
            envelope = ZLinkRelocationEnvelopeCodec.Decode(root.Span);
        }
        catch (Exception error) when (error is InvalidDataException
                                      or ArgumentException
                                      or EndOfStreamException)
        {
            throw new ZLinkRelocationDataLostException(
                $"Published relocation root '{publication.Reference}' is malformed.");
        }
        if (envelope.AggregateId != publication.AggregateId
            || envelope.AggregateGeneration != publication.AggregateGeneration
            || !envelope.InventoryDigest.Span.SequenceEqual(
                publication.InventoryDigest.Span))
            throw new ZLinkRelocationDataLostException(
                $"Published relocation root '{publication.Reference}' does not match its authority manifest.");

        return new ZLinkPublishedRelocation(
            authority,
            new ZLinkRelocationStored(
                publication.Reference,
                publication.ChecksumCrc32c,
                default,
                authority.StoreNow),
            envelope);
    }

    private async ValueTask VerifyStoredRootAsync(
        ZLinkRelocationStored stored,
        ReadOnlyMemory<byte> expected,
        CancellationToken cancellationToken)
    {
        var read = await relocationStore.GetRelocationAsync(
                stored.Reference,
                cancellationToken)
            .ConfigureAwait(false);
        if (read is not ZLinkRelocationReadResult.Found found
            || !found.Payload.Span.SequenceEqual(expected.Span))
            throw new ZLinkRelocationDataLostException(
                $"Relocation Store did not preserve immutable root '{stored.Reference}'.");
    }

    private async ValueTask<ZLinkAuthorityReadResult?> TryReadAuthorityWithoutCancellationAsync(
        ZLinkAuthorityKey key)
    {
        try
        {
            return await authorityStore.ReadAuthorityAsync(key, CancellationToken.None)
                .ConfigureAwait(false);
        }
        catch
        {
            return null;
        }
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
            // The fixed 24-hour retention remains the final orphan cleanup.
        }
    }

    private static void ValidateRequest(ZLinkRelocationPublicationRequest request)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(request.AuthorityKey.Value);
        ArgumentException.ThrowIfNullOrWhiteSpace(request.TargetOwnerId);
        if (request.TargetOwnerLeaseGeneration <= 0)
            throw new ArgumentOutOfRangeException(
                nameof(request),
                "The target owner lease generation must be positive.");
        if (request.GenerationTransition
                == ZLinkAuthorityGenerationTransition.NewOwner
            && request.RelocationCapacityFence is null
            || request.GenerationTransition
                != ZLinkAuthorityGenerationTransition.NewOwner
            && request.RelocationCapacityFence is not null)
            throw new ArgumentException(
                "Only NewOwner publication requires a relocation capacity fence.",
                nameof(request));
        if (request.ApplicationAuthorityPayload.Length > 1024 * 1024)
            throw new ArgumentOutOfRangeException(
                nameof(request),
                "The application authority payload cannot exceed 1 MiB.");
    }

    private static void ValidateStored(
        ReadOnlyMemory<byte> root,
        ZLinkRelocationStored stored)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(stored.Reference);
        if (stored.ChecksumCrc32c != ZLinkCrc32C.Compute(root.Span))
            throw new ZLinkRelocationDataLostException(
                "Relocation Store returned a checksum that does not match the immutable root.");
        if (stored.ExpiresAt <= stored.StoreNow)
            throw new InvalidDataException(
                "Relocation Store returned a non-positive retention interval.");
    }

    private static void ValidatePublishedSnapshot(
        ZLinkAuthoritySnapshot snapshot,
        ZLinkRelocationPublicationRequest request,
        ZLinkRelocationStored stored)
    {
        if (snapshot.ObjectGeneration is 0 or > long.MaxValue
            || snapshot.AuthorityOwnerGeneration is 0 or > long.MaxValue)
            throw new InvalidDataException(
                "Authority Store returned an invalid generation.");
        if (!string.Equals(
                snapshot.OwnerId,
                request.TargetOwnerId,
                StringComparison.Ordinal)
            || snapshot.OwnerLeaseGeneration != request.TargetOwnerLeaseGeneration)
            throw new InvalidDataException(
                "Authority Store did not publish the requested target owner fence.");
        if (!ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                snapshot.Payload.Span,
                out var publication)
            || !string.Equals(
                publication.Reference,
                stored.Reference,
                StringComparison.Ordinal)
            || publication.ChecksumCrc32c != stored.ChecksumCrc32c)
            throw new InvalidDataException(
                "Authority Store did not preserve the relocation publication.");
    }
}

internal sealed record ZLinkRelocationAuthorityPayload(
    string Reference,
    uint ChecksumCrc32c,
    Guid AggregateId,
    ulong AggregateGeneration,
    ReadOnlyMemory<byte> InventoryDigest,
    string TargetOwnerId,
    long TargetOwnerLeaseGeneration,
    ReadOnlyMemory<byte> ApplicationPayload);

internal static class ZLinkRelocationAuthorityPayloadCodec
{
    private const uint Magic = 0x5a4c4152; // ZLAR
    private const ushort Version = 1;

    internal static byte[] Encode(ZLinkRelocationAuthorityPayload payload)
    {
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream, Encoding.UTF8, leaveOpen: true);
        writer.Write(Magic);
        writer.Write(Version);
        WriteString(writer, payload.Reference);
        writer.Write(payload.ChecksumCrc32c);
        writer.Write(payload.AggregateId.ToByteArray());
        writer.Write(payload.AggregateGeneration);
        WriteBytes(writer, payload.InventoryDigest.Span);
        WriteString(writer, payload.TargetOwnerId);
        writer.Write(payload.TargetOwnerLeaseGeneration);
        WriteBytes(writer, payload.ApplicationPayload.Span);
        writer.Flush();
        if (stream.Length > 1024 * 1024)
            throw new InvalidOperationException(
                "The authority relocation payload cannot exceed 1 MiB.");
        return stream.ToArray();
    }

    internal static bool TryDecode(
        ReadOnlySpan<byte> encoded,
        out ZLinkRelocationAuthorityPayload payload)
    {
        payload = null!;
        try
        {
            using var stream = new MemoryStream(encoded.ToArray(), writable: false);
            using var reader = new BinaryReader(stream, Encoding.UTF8, leaveOpen: true);
            if (reader.ReadUInt32() != Magic || reader.ReadUInt16() != Version)
                return false;
            var reference = ReadString(reader);
            var checksum = reader.ReadUInt32();
            var aggregateId = new Guid(ReadExact(reader, 16));
            var aggregateGeneration = reader.ReadUInt64();
            var inventoryDigest = ReadBytes(reader);
            var targetOwnerId = ReadString(reader);
            var targetLeaseGeneration = reader.ReadInt64();
            var applicationPayload = ReadBytes(reader);
            if (aggregateId == Guid.Empty
                || aggregateGeneration is 0 or > long.MaxValue
                || inventoryDigest.Length != 32
                || targetLeaseGeneration <= 0
                || stream.Position != stream.Length)
                return false;
            payload = new ZLinkRelocationAuthorityPayload(
                reference,
                checksum,
                aggregateId,
                aggregateGeneration,
                inventoryDigest,
                targetOwnerId,
                targetLeaseGeneration,
                applicationPayload);
            return true;
        }
        catch (Exception error) when (error is IOException
                                      or ArgumentException
                                      or OverflowException)
        {
            return false;
        }
    }

    private static void WriteString(BinaryWriter writer, string value)
    {
        var encoded = Encoding.UTF8.GetBytes(value);
        if (encoded.Length is < 1 or > ushort.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(value));
        writer.Write((ushort)encoded.Length);
        writer.Write(encoded);
    }

    private static string ReadString(BinaryReader reader)
    {
        var size = reader.ReadUInt16();
        if (size == 0)
            throw new InvalidDataException();
        return Encoding.UTF8.GetString(ReadExact(reader, size));
    }

    private static void WriteBytes(BinaryWriter writer, ReadOnlySpan<byte> value)
    {
        writer.Write(value.Length);
        writer.Write(value);
    }

    private static byte[] ReadBytes(BinaryReader reader)
    {
        var size = reader.ReadInt32();
        if (size < 0 || size > 1024 * 1024)
            throw new InvalidDataException();
        return ReadExact(reader, size);
    }

    private static byte[] ReadExact(BinaryReader reader, int size)
    {
        var value = reader.ReadBytes(size);
        if (value.Length != size)
            throw new EndOfStreamException();
        return value;
    }
}
