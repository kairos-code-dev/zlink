using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.Runtime.Actors;

internal enum ZLinkDeferredJoinCompletionCursor : byte
{
    Prepared = 1,
    Committed = 2,
    Delivered = 3
}

internal sealed record ZLinkDeferredJoinCompletionRecord(
    string ActorId,
    ulong ObjectGeneration,
    ZLinkActorJoinOperationId OperationId,
    ActorRef Actor,
    string? ReplyContentType,
    ReadOnlyMemory<byte> Reply,
    ZLinkDeferredJoinCompletionCursor Cursor);

internal sealed record ZLinkDeferredJoinCompletionRoot(
    ZLinkAuthorityKey AuthorityKey,
    string Reference,
    uint ChecksumCrc32c,
    string ExpectedStoreVersion,
    ZLinkRelocationEnvelope Envelope,
    ZLinkDeferredJoinCompletionRecord Completion);

/// <summary>
/// Persists the post-commit Actor Join completion in the relocation root that
/// the Actor authority publishes. Updating a cursor always writes a new
/// immutable root before the authority CAS; the old root is removed only after
/// the CAS stops referencing it.
/// </summary>
internal sealed class ZLinkDeferredActorJoinCompletionJournal(
    IZLinkLocationRepository authorityStore,
    IZLinkRelocationRepository relocationStore)
{
    private static readonly TimeSpan Retention = TimeSpan.FromHours(24);

    internal async ValueTask<ZLinkDeferredJoinCompletionRoot> PrepareAsync(
        string actorId,
        ZLinkActorJoinOperationId operationId,
        ActorRef actor,
        string? replyContentType,
        ReadOnlyMemory<byte> reply,
        CancellationToken cancellationToken)
    {
        ValidateIdentity(actorId, operationId, actor);
        var key = ZLinkActorAuthorityPayloadCodec.AuthorityKey(actorId);
        var read = await authorityStore.ReadAuthorityAsync(key, cancellationToken)
            .ConfigureAwait(false);
        if (read is not ZLinkAuthorityReadResult.Found found)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.NotFound,
                $"Actor '{actorId}' does not have authority for durable Join completion.");

        var snapshot = found.Snapshot;
        if (snapshot.ObjectGeneration != actor.ObjectGeneration)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.InvalidOperation,
                $"Actor '{actorId}' authority generation changed before Join completion was prepared.");

        if (await TryReadPublishedAsync(key, snapshot, cancellationToken).ConfigureAwait(false)
            is { } existing)
        {
            EnsureSameOperation(existing.Completion, operationId, actor);
            return existing;
        }

        if (!ZLinkActorAuthorityPayloadCodec.TryDecodeRelocating(
                snapshot.Payload.Span,
                out _))
            throw new InvalidDataException(
                $"Actor '{actorId}' authority payload cannot host a relocation manifest.");

        var completion = new ZLinkDeferredJoinCompletionRecord(
            actorId,
            actor.ObjectGeneration,
            operationId,
            actor,
            replyContentType,
            reply.ToArray(),
            ZLinkDeferredJoinCompletionCursor.Prepared);
        var replacedReference = default(string);
        var replacedRootIsActorOnly = false;
        var applicationAuthorityPayload = snapshot.Payload;
        ZLinkRelocationEnvelope envelope;
        if (ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                snapshot.Payload.Span,
                out var currentPublication))
        {
            var current = await new ZLinkRelocationPublicationCoordinator(
                    authorityStore,
                    relocationStore)
                .RecoverAsync(key, cancellationToken)
                .ConfigureAwait(false)
                ?? throw new ZLinkRelocationDataLostException(
                    $"Actor '{actorId}' relocation publication disappeared.");
            if (current.Envelope.Participants.Count != 1
                || current.Envelope.Participants[0].AuthorityKey != key)
                throw new ZLinkRelocationDataLostException(
                    $"Actor '{actorId}' Join completion cannot replace another relocation aggregate.");
            var foundParticipant = false;
            envelope = current.Envelope with
            {
                Participants = current.Envelope.Participants.Select(
                        participant =>
                        {
                            if (participant.AuthorityKey != key)
                                return participant;
                            foundParticipant = true;
                            return participant with
                            {
                                CompletionPayload =
                                    ZLinkDeferredJoinCompletionCodec.Encode(
                                        completion)
                            };
                        })
                    .ToArray()
            };
            if (!foundParticipant)
                throw new ZLinkRelocationDataLostException(
                    $"Actor '{actorId}' relocation root does not contain its authority participant.");
            replacedReference = current.Relocation.Reference;
            replacedRootIsActorOnly = current.Envelope.Participants.Count == 1;
            applicationAuthorityPayload = currentPublication.ApplicationPayload;
        }
        else
        {
            envelope = CreateEnvelope(key, snapshot, completion);
        }
        var coordinator = new ZLinkRelocationPublicationCoordinator(
            authorityStore,
            relocationStore);
        var published = await coordinator.PublishAsync(
                new ZLinkRelocationPublicationRequest(
                    key,
                    snapshot.StoreVersion,
                    ZLinkAuthorityGenerationTransition.Preserve,
                    snapshot.OwnerId,
                    snapshot.OwnerLeaseGeneration,
                    applicationAuthorityPayload,
                    null,
                    envelope),
                cancellationToken)
            .ConfigureAwait(false);
        if (replacedRootIsActorOnly
            && replacedReference is not null
            && !string.Equals(
                replacedReference,
                published.Relocation.Reference,
                StringComparison.Ordinal))
            await DeleteBestEffortAsync(replacedReference).ConfigureAwait(false);
        return new ZLinkDeferredJoinCompletionRoot(
            key,
            published.Relocation.Reference,
            published.Relocation.ChecksumCrc32c,
            published.Authority.StoreVersion,
            envelope,
            completion);
    }

    internal ValueTask<ZLinkDeferredJoinCompletionRoot> MarkCommittedAsync(
        ZLinkDeferredJoinCompletionRoot root,
        CancellationToken cancellationToken) =>
        MoveCursorAsync(root, ZLinkDeferredJoinCompletionCursor.Committed, cancellationToken);

    internal ValueTask<ZLinkDeferredJoinCompletionRoot> MarkDeliveredAsync(
        ZLinkDeferredJoinCompletionRoot root,
        CancellationToken cancellationToken) =>
        MoveCursorAsync(root, ZLinkDeferredJoinCompletionCursor.Delivered, cancellationToken);

    internal async ValueTask<ZLinkDeferredJoinCompletionRoot?> RecoverAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        var key = ZLinkActorAuthorityPayloadCodec.AuthorityKey(actorId);
        var read = await authorityStore.ReadAuthorityAsync(key, cancellationToken)
            .ConfigureAwait(false);
        return read is ZLinkAuthorityReadResult.Found found
            ? await TryReadPublishedAsync(key, found.Snapshot, cancellationToken)
                .ConfigureAwait(false)
            : null;
    }

    internal async ValueTask<ZLinkAuthoritySnapshot?> ReleaseAsync(
        ZLinkDeferredJoinCompletionRoot root,
        CancellationToken cancellationToken)
    {
        if (root.Completion.Cursor != ZLinkDeferredJoinCompletionCursor.Delivered)
            throw new InvalidOperationException(
                "A deferred Join completion root cannot be released before delivery.");

        var read = await authorityStore.ReadAuthorityAsync(root.AuthorityKey, cancellationToken)
            .ConfigureAwait(false);
        if (read is not ZLinkAuthorityReadResult.Found found
            || !ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                found.Snapshot.Payload.Span,
                out var publication)
            || !string.Equals(publication.Reference, root.Reference, StringComparison.Ordinal)
            || publication.ChecksumCrc32c != root.ChecksumCrc32c)
            return null;

        var result = await authorityStore.CompareExchangeAuthorityAsync(
                root.AuthorityKey,
                found.Snapshot.StoreVersion,
                new ZLinkAuthorityMutation.Put(
                    publication.ApplicationPayload,
                    ZLinkAuthorityGenerationTransition.Preserve,
                    null,
                    null),
                cancellationToken)
            .ConfigureAwait(false);
        if (result is not ZLinkAuthorityCompareExchangeResult.Stored stored)
            return null;

        await DeleteBestEffortAsync(root.Reference).ConfigureAwait(false);
        return stored.Snapshot;
    }

    private async ValueTask<ZLinkDeferredJoinCompletionRoot> MoveCursorAsync(
        ZLinkDeferredJoinCompletionRoot root,
        ZLinkDeferredJoinCompletionCursor next,
        CancellationToken cancellationToken)
    {
        if ((byte)next < (byte)root.Completion.Cursor
            || (byte)next > (byte)root.Completion.Cursor + 1)
            throw new InvalidOperationException("Deferred Join completion cursor transition is invalid.");
        if (next == root.Completion.Cursor) return root;

        var read = await authorityStore.ReadAuthorityAsync(root.AuthorityKey, cancellationToken)
            .ConfigureAwait(false);
        if (read is not ZLinkAuthorityReadResult.Found found)
            throw new ZLinkRelocationDataLostException(
                $"Actor '{root.Completion.ActorId}' authority disappeared during Join completion.");
        var snapshot = found.Snapshot;
        var current = await TryReadPublishedAsync(
                root.AuthorityKey,
                snapshot,
                cancellationToken)
            .ConfigureAwait(false)
            ?? throw new ZLinkRelocationDataLostException(
                $"Actor '{root.Completion.ActorId}' no longer references its Join completion root.");
        EnsureSameOperation(
            current.Completion,
            root.Completion.OperationId,
            root.Completion.Actor);
        if ((byte)current.Completion.Cursor >= (byte)next) return current;

        var updatedCompletion = current.Completion with { Cursor = next };
        var updatedEnvelope = current.Envelope with
        {
            Participants = current.Envelope.Participants.Select(
                    participant => participant.AuthorityKey
                                   == root.AuthorityKey
                        ? participant with
                        {
                    CompletionPayload =
                        ZLinkDeferredJoinCompletionCodec.Encode(
                                    updatedCompletion)
                        }
                        : participant)
                .ToArray()
        };
        var tree = await ZLinkRelocationTreeStore.PutAsync(
                relocationStore,
                updatedEnvelope,
                Retention,
                cancellationToken)
            .ConfigureAwait(false);
        var stored = tree.Root;

        if (!ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                snapshot.Payload.Span,
                out var publication))
            throw new ZLinkRelocationDataLostException(
                "Actor authority lost its relocation publication.");
        var nextPayload = ZLinkRelocationAuthorityPayloadCodec.Encode(
            publication with
            {
                Reference = stored.Reference,
                ChecksumCrc32c = stored.ChecksumCrc32c
            });
        var result = await authorityStore.CompareExchangeAuthorityAsync(
                root.AuthorityKey,
                snapshot.StoreVersion,
                new ZLinkAuthorityMutation.Put(
                    nextPayload,
                    ZLinkAuthorityGenerationTransition.Preserve,
                    null,
                    null),
                cancellationToken)
            .ConfigureAwait(false);
        if (result is not ZLinkAuthorityCompareExchangeResult.Stored success)
        {
            await DeleteBestEffortAsync(stored.Reference).ConfigureAwait(false);
            return await RecoverAsync(root.Completion.ActorId, cancellationToken)
                       .ConfigureAwait(false)
                   ?? throw new ZLinkRelocationDataLostException(
                       "Deferred Join completion cursor CAS conflicted without a recoverable root.");
        }

        await DeleteBestEffortAsync(current.Reference).ConfigureAwait(false);
        return new ZLinkDeferredJoinCompletionRoot(
            root.AuthorityKey,
            stored.Reference,
            stored.ChecksumCrc32c,
            success.Snapshot.StoreVersion,
            updatedEnvelope,
            updatedCompletion);
    }

    private async ValueTask<ZLinkDeferredJoinCompletionRoot?> TryReadPublishedAsync(
        ZLinkAuthorityKey key,
        ZLinkAuthoritySnapshot snapshot,
        CancellationToken cancellationToken)
    {
        if (!ZLinkRelocationAuthorityPayloadCodec.TryDecode(
                snapshot.Payload.Span,
                out var publication)
            || publication.IsCanonical)
            return null;
        var envelope = await ZLinkRelocationTreeStore.GetAsync(
                relocationStore,
                publication.Reference,
                publication.ChecksumCrc32c,
                cancellationToken)
            .ConfigureAwait(false);
        var participant = envelope.Participants.SingleOrDefault(
            item => item.AuthorityKey == key);
        if (participant is null)
            throw new ZLinkRelocationDataLostException(
                "Deferred Join completion manifest does not match Actor authority.");
        if (participant.CompletionPayload.IsEmpty)
            return null;
        var completion = ZLinkDeferredJoinCompletionCodec.Decode(
            participant.CompletionPayload.Span);
        return new ZLinkDeferredJoinCompletionRoot(
            key,
            publication.Reference,
            publication.ChecksumCrc32c,
            snapshot.StoreVersion,
            envelope,
            completion);
    }

    private static ZLinkRelocationEnvelope CreateEnvelope(
        ZLinkAuthorityKey key,
        ZLinkAuthoritySnapshot snapshot,
        ZLinkDeferredJoinCompletionRecord completion)
    {
        var inventory = SHA256.HashData(Encoding.UTF8.GetBytes(key.Value));
        return new ZLinkRelocationEnvelope(
            Guid.NewGuid(),
            snapshot.AuthorityOwnerGeneration,
            inventory,
            [
                new ZLinkRelocationParticipantEnvelope(
                    key,
                    ZLinkPlacementObjectKind.Actor,
                    snapshot.ObjectGeneration,
                    snapshot.AuthorityOwnerGeneration,
                    ReadOnlyMemory<byte>.Empty,
                    Array.Empty<ZLinkRelocationQueuedJob>(),
                    Array.Empty<ZLinkRelocationLogicalTimer>(),
                    ReadOnlyMemory<byte>.Empty,
                    ZLinkDeferredJoinCompletionCodec.Encode(completion))
            ]);
    }

    private async ValueTask DeleteBestEffortAsync(string reference)
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
            // The fixed retention remains the orphan cleanup boundary.
        }
    }

    private static void ValidateIdentity(
        string actorId,
        ZLinkActorJoinOperationId operationId,
        ActorRef actor)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(actorId);
        if (operationId.High == 0 && operationId.Low == 0)
            throw new ArgumentOutOfRangeException(nameof(operationId));
        if (!string.Equals(actor.ActorId, actorId, StringComparison.Ordinal)
            || actor.ObjectGeneration == 0)
            throw new ArgumentException("Deferred Join completion Actor identity is invalid.");
    }

    private static void EnsureSameOperation(
        ZLinkDeferredJoinCompletionRecord completion,
        ZLinkActorJoinOperationId operationId,
        ActorRef actor)
    {
        if (completion.OperationId != operationId
            || completion.Actor != actor
            || completion.ObjectGeneration != actor.ObjectGeneration)
            throw new InvalidOperationException(
                $"Actor '{actor.ActorId}' already has a different durable Join completion.");
    }
}

internal static class ZLinkDeferredJoinCompletionCodec
{
    private const uint Magic = 0x5a4c4a43; // ZLJC
    private const byte Version = 2;
    private const int MaxBytes = 1024 * 1024;

    internal static byte[] Encode(ZLinkDeferredJoinCompletionRecord value)
    {
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream, Encoding.UTF8, leaveOpen: true);
        writer.Write(Magic);
        writer.Write(Version);
        WriteString(writer, value.ActorId);
        writer.Write(value.ObjectGeneration);
        writer.Write(value.OperationId.High);
        writer.Write(value.OperationId.Low);
        WriteString(writer, value.Actor.MeshName);
        WriteRoutingId(writer, value.Actor.NodeRid);
        writer.Write(value.Actor.ObjectGeneration);
        writer.Write(value.ReplyContentType is not null);
        if (value.ReplyContentType is { } contentType) WriteString(writer, contentType);
        writer.Write(value.Reply.Length);
        writer.Write(value.Reply.Span);
        writer.Write((byte)value.Cursor);
        writer.Flush();
        if (stream.Length > MaxBytes) throw new InvalidDataException();
        return stream.ToArray();
    }

    internal static ZLinkDeferredJoinCompletionRecord Decode(ReadOnlySpan<byte> encoded)
    {
        if (encoded.Length is <= 0 or > MaxBytes) throw new InvalidDataException();
        using var stream = new MemoryStream(encoded.ToArray(), writable: false);
        using var reader = new BinaryReader(stream, Encoding.UTF8, leaveOpen: true);
        if (reader.ReadUInt32() != Magic || reader.ReadByte() != Version)
            throw new InvalidDataException();
        var actorId = ReadString(reader);
        var generation = reader.ReadUInt64();
        var operation = new ZLinkActorJoinOperationId(
            reader.ReadUInt64(),
            reader.ReadUInt64());
        var meshName = ReadString(reader);
        var nodeRid = ReadRoutingId(reader);
        var actorGeneration = reader.ReadUInt64();
        var contentType = reader.ReadBoolean() ? ReadString(reader) : null;
        var replyLength = reader.ReadInt32();
        if (replyLength < 0 || replyLength > MaxBytes) throw new InvalidDataException();
        var reply = reader.ReadBytes(replyLength);
        if (reply.Length != replyLength) throw new EndOfStreamException();
        var cursor = (ZLinkDeferredJoinCompletionCursor)reader.ReadByte();
        if (generation == 0
            || actorGeneration != generation
            || operation.High == 0 && operation.Low == 0
            || !Enum.IsDefined(cursor)
            || stream.Position != stream.Length)
            throw new InvalidDataException();
        return new ZLinkDeferredJoinCompletionRecord(
            actorId,
            generation,
            operation,
            new ActorRef(actorId, generation, meshName, nodeRid),
            contentType,
            reply,
            cursor);
    }

    private static void WriteString(BinaryWriter writer, string value)
    {
        var bytes = Encoding.UTF8.GetBytes(value);
        if (bytes.Length is < 1 or > ushort.MaxValue) throw new InvalidDataException();
        writer.Write((ushort)bytes.Length);
        writer.Write(bytes);
    }

    private static string ReadString(BinaryReader reader)
    {
        var length = reader.ReadUInt16();
        if (length == 0) throw new InvalidDataException();
        var bytes = reader.ReadBytes(length);
        if (bytes.Length != length) throw new EndOfStreamException();
        return Encoding.UTF8.GetString(bytes);
    }

    private static void WriteRoutingId(BinaryWriter writer, RoutingId value)
    {
        var bytes = value.ToBytes();
        if (bytes.Length is < 1 or > byte.MaxValue) throw new InvalidDataException();
        writer.Write((byte)bytes.Length);
        writer.Write(bytes);
    }

    private static RoutingId ReadRoutingId(BinaryReader reader)
    {
        var length = reader.ReadByte();
        if (length == 0) throw new InvalidDataException();
        var bytes = reader.ReadBytes(length);
        if (bytes.Length != length) throw new EndOfStreamException();
        return RoutingId.From(bytes);
    }
}
