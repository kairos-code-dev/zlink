using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.Runtime.Actors;

internal sealed record ZLinkActorRelocationRecoveryRecord(
    ZLinkRemoteActorJoinRequest Request,
    string TargetSpotId,
    byte[] TargetNodeRid,
    ulong TargetNodeGeneration,
    ulong TargetSpotGeneration,
    ulong TargetAuthorityOwnerGeneration,
    ulong OperationIdHigh,
    ulong OperationIdLow,
    string? ReplyContentType,
    byte[] Reply);

internal sealed record ZLinkLoadedActorRelocation(
    ZLinkRelocationEnvelope Envelope,
    ZLinkRelocationParticipantEnvelope Participant,
    ZLinkActorRelocationRecoveryRecord Recovery,
    IReadOnlyList<ZLinkActorHandoffFrame> AcceptedFrames);

internal static class ZLinkActorRelocationRoot
{
    internal static ZLinkRelocationEnvelope Create(
        ZLinkAuthorityKey authorityKey,
        ulong objectGeneration,
        ulong authorityOwnerGeneration,
        Guid relocationId,
        ReadOnlyMemory<byte> applicationState,
        IReadOnlyList<ZLinkActorHandoffFrame> acceptedFrames,
        ZLinkActorRelocationRecoveryRecord recovery)
    {
        if (relocationId == Guid.Empty)
            throw new ArgumentOutOfRangeException(nameof(relocationId));
        var inventoryDigest = ComputeInventoryDigest(
            authorityKey,
            ZLinkPlacementObjectKind.Actor,
            objectGeneration,
            authorityOwnerGeneration,
            recovery);
        var durableRecovery = recovery with
        {
            Request = recovery.Request with
            {
                RelocationInventoryDigest = inventoryDigest
            }
        };
        return new ZLinkRelocationEnvelope(
            relocationId,
            authorityOwnerGeneration,
            inventoryDigest,
            [
                new ZLinkRelocationParticipantEnvelope(
                    authorityKey,
                    ZLinkPlacementObjectKind.Actor,
                    objectGeneration,
                    authorityOwnerGeneration,
                    applicationState,
                    acceptedFrames
                        .OrderBy(static frame => frame.ArrivalIndex)
                        .Select(
                            (frame, index) => new ZLinkRelocationQueuedJob(
                                checked((ulong)index + 1),
                                JsonSerializer.SerializeToUtf8Bytes(frame)))
                        .ToArray(),
                    [],
                    JsonSerializer.SerializeToUtf8Bytes(durableRecovery))
            ]);
    }

    internal static ZLinkLoadedActorRelocation Load(
        ZLinkRemoteActorJoinRequest wire,
        ZLinkRelocationEnvelope envelope)
    {
        var key = ZLinkActorAuthorityPayloadCodec.AuthorityKey(wire.ActorId);
        if (envelope.AggregateId != wire.RelocationAggregateId
            || envelope.AggregateGeneration
            != wire.RelocationAggregateGeneration
            || !envelope.InventoryDigest.Span.SequenceEqual(
                wire.RelocationInventoryDigest)
            || envelope.Participants.Count != 1
            || envelope.Participants[0].AuthorityKey != key)
            throw DataLost(
                $"Actor '{wire.ActorId}' relocation manifest does not match its durable root.");
        var participant = envelope.Participants[0];
        if (participant.ObjectKind != ZLinkPlacementObjectKind.Actor
            || participant.ObjectGeneration != wire.ActorGeneration
            || participant.AuthorityOwnerGeneration
            != wire.ActorAuthorityOwnerGeneration
            || participant.RecoveryPayload.IsEmpty)
            throw DataLost(
                $"Actor '{wire.ActorId}' relocation participant fences are invalid.");

        ZLinkActorRelocationRecoveryRecord recovery;
        ZLinkActorHandoffFrame[] frames;
        try
        {
            recovery = JsonSerializer.Deserialize<
                           ZLinkActorRelocationRecoveryRecord>(
                           participant.RecoveryPayload.Span)
                       ?? throw new JsonException();
            frames = participant.AcceptedJobs.Select(
                    job => JsonSerializer.Deserialize<ZLinkActorHandoffFrame>(
                               job.Payload.Span)
                           ?? throw new JsonException())
                .ToArray();
        }
        catch (Exception error) when (error is JsonException
                                      or NotSupportedException)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RelocationDataLost,
                $"Actor '{wire.ActorId}' relocation recovery payload is malformed.",
                isRetriable: false,
                error);
        }
        var expectedInventoryDigest = ComputeInventoryDigest(
            participant.AuthorityKey,
            participant.ObjectKind,
            participant.ObjectGeneration,
            participant.AuthorityOwnerGeneration,
            recovery);
        if (!envelope.InventoryDigest.Span.SequenceEqual(
                expectedInventoryDigest))
            throw DataLost(
                $"Actor '{wire.ActorId}' relocation inventory mutation is invalid.");
        if (!string.Equals(
                recovery.Request.ActorId,
                wire.ActorId,
                StringComparison.Ordinal)
            || !string.Equals(
                recovery.Request.ActorType,
                wire.ActorType,
                StringComparison.Ordinal)
            || !string.Equals(
                recovery.Request.HandoffId,
                wire.HandoffId,
                StringComparison.Ordinal)
            || !string.Equals(
                recovery.TargetSpotId,
                ZLinkSpotId.Require(
                    recovery.TargetSpotId,
                    nameof(recovery.TargetSpotId)),
                StringComparison.Ordinal)
            || recovery.TargetNodeRid is not { Length: > 0 }
            || recovery.TargetNodeGeneration == 0
            || recovery.TargetSpotGeneration == 0
            || recovery.TargetAuthorityOwnerGeneration == 0
            || recovery.Request.TargetNodeRid is null
            || !recovery.TargetNodeRid.AsSpan().SequenceEqual(
                recovery.Request.TargetNodeRid)
            || recovery.TargetNodeGeneration
               != recovery.Request.TargetNodeGeneration
            || recovery.TargetSpotGeneration
               != recovery.Request.TargetSpotGeneration
            || recovery.TargetAuthorityOwnerGeneration
               != recovery.Request.TargetAuthorityOwnerGeneration)
            throw DataLost(
                $"Actor '{wire.ActorId}' relocation recovery identity is invalid.");
        var normalizedWire = wire with
        {
            RelocationReference =
                recovery.Request.RelocationReference,
            RelocationChecksumCrc32c =
                recovery.Request.RelocationChecksumCrc32c,
            RelocationAggregateId =
                recovery.Request.RelocationAggregateId,
            RelocationAggregateGeneration =
                recovery.Request.RelocationAggregateGeneration,
            RelocationInventoryDigest =
                recovery.Request.RelocationInventoryDigest,
            HandoffFrames = recovery.Request.HandoffFrames
        };
        if (!ZLinkActorHandoffRequestIdentity.Matches(
                recovery.Request,
                normalizedWire))
            throw DataLost(
                $"Actor '{wire.ActorId}' relocation wire changed its durable route or session fences.");
        return new ZLinkLoadedActorRelocation(
            envelope,
            participant,
            recovery,
            frames);
    }

    internal static ZLinkRelocationManifestReference Reference(
        ZLinkRemoteActorJoinRequest request) =>
        new(
            request.RelocationReference,
            request.RelocationChecksumCrc32c,
            request.RelocationAggregateId,
            request.RelocationAggregateGeneration,
            request.RelocationInventoryDigest);

    internal static ZLinkRemoteActorJoinRequest WithDurableFrames(
        ZLinkRemoteActorJoinRequest wire,
        ZLinkLoadedActorRelocation loaded) =>
        wire with
        {
            HandoffFrames = loaded.AcceptedFrames
        };

    private static byte[] ComputeInventoryDigest(
        ZLinkAuthorityKey authorityKey,
        ZLinkPlacementObjectKind objectKind,
        ulong objectGeneration,
        ulong authorityOwnerGeneration,
        ZLinkActorRelocationRecoveryRecord recovery)
    {
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(
            stream,
            Encoding.UTF8,
            leaveOpen: true);
        var request = recovery.Request;
        WriteString(writer, authorityKey.Value);
        writer.Write((byte)objectKind);
        writer.Write(objectGeneration);
        writer.Write(authorityOwnerGeneration);

        // Canonically bind the target membership and every route, generation,
        // and session fence preserved by the authority mutation.
        WriteString(writer, recovery.TargetSpotId);
        WriteBytes(writer, recovery.TargetNodeRid);
        writer.Write(recovery.TargetNodeGeneration);
        writer.Write(recovery.TargetSpotGeneration);
        writer.Write(recovery.TargetAuthorityOwnerGeneration);
        WriteString(writer, request.ActorId);
        WriteString(writer, request.ActorType);
        WriteString(writer, request.HandoffId);
        WriteBytes(writer, request.SourceNodeRid);
        WriteString(writer, request.SourceSpotId);
        writer.Write(request.ActorGeneration);
        writer.Write(request.ActorAuthorityOwnerGeneration);
        WriteString(writer, request.ReservationToken);
        writer.Write(request.ReservedPayloadBytes);
        WriteNullableBytes(writer, request.TargetNodeRid);
        writer.Write(request.TargetNodeGeneration);
        writer.Write(request.TargetSpotGeneration);
        writer.Write(request.TargetAuthorityOwnerGeneration);
        writer.Write(request.TargetSpotAuthorityOwnerGeneration);
        WriteNullableBytes(writer, request.BoundSessionNodeRid);
        WriteNullableBytes(writer, request.BoundSessionRid);
        WriteNullableString(writer, request.BoundSessionBindingToken);
        writer.Write(request.BoundSessionBindingGeneration);
        writer.Write(request.BoundSessionObjectGeneration);
        writer.Write(request.BoundSessionAuthorityOwnerGeneration);
        WriteString(writer, request.BoundSessionMeshName ?? string.Empty);
        writer.Write(request.BoundSessionTargetNodeGeneration);
        writer.Write(request.BoundSessionOwnerLeaseGeneration);
        writer.Write(request.BoundSessionOwnerNodeGeneration);
        writer.Write(request.BoundSessionAcceptedHighWater);
        WriteString(writer, request.RelocationContentType);
        writer.Flush();
        return SHA256.HashData(
            stream.GetBuffer().AsSpan(0, checked((int)stream.Length)));
    }

    private static void WriteString(BinaryWriter writer, string value)
    {
        var bytes = Encoding.UTF8.GetBytes(value);
        writer.Write(bytes.Length);
        writer.Write(bytes);
    }

    private static void WriteNullableString(
        BinaryWriter writer,
        string? value)
    {
        writer.Write(value is not null);
        if (value is not null)
            WriteString(writer, value);
    }

    private static void WriteBytes(BinaryWriter writer, byte[] value)
    {
        writer.Write(value.Length);
        writer.Write(value);
    }

    private static void WriteNullableBytes(
        BinaryWriter writer,
        byte[]? value)
    {
        writer.Write(value is not null);
        if (value is not null)
            WriteBytes(writer, value);
    }

    private static ZLinkFrameworkException DataLost(string message) =>
        new(
            ZLinkFrameworkErrorKind.RelocationDataLost,
            message,
            isRetriable: false);
}
