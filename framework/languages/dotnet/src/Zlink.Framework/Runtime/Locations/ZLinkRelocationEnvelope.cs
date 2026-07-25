using System.Buffers.Binary;
using System.Text;
using System.Security.Cryptography;

namespace Zlink.Framework.Runtime.Locations;

internal sealed record ZLinkRelocationQueuedJob(
    ulong AcceptedSequence,
    ReadOnlyMemory<byte> Payload);

internal sealed record ZLinkRelocationLogicalTimer(
    string TimerId,
    long DueUnixTimeMilliseconds,
    long PeriodMilliseconds,
    ReadOnlyMemory<byte> Payload);

internal sealed record ZLinkRelocationParticipantEnvelope(
    ZLinkAuthorityKey AuthorityKey,
    ZLinkPlacementObjectKind ObjectKind,
    ulong ObjectGeneration,
    ulong AuthorityOwnerGeneration,
    ReadOnlyMemory<byte> ApplicationState,
    IReadOnlyList<ZLinkRelocationQueuedJob> AcceptedJobs,
    IReadOnlyList<ZLinkRelocationLogicalTimer> LogicalTimers,
    ReadOnlyMemory<byte> RecoveryPayload = default,
    ReadOnlyMemory<byte> CompletionPayload = default);

internal sealed record ZLinkRelocationEnvelope(
    Guid AggregateId,
    ulong AggregateGeneration,
    ReadOnlyMemory<byte> InventoryDigest,
    IReadOnlyList<ZLinkRelocationParticipantEnvelope> Participants)
{
    // A decoded protocol-schema logical stream is retained byte-for-byte.  The
    // current runtime projection does not expose every frozen-record field yet,
    // so re-encoding the projection would silently discard durable information.
    internal ReadOnlyMemory<byte> CanonicalLogicalStream { get; init; }
}

internal static class ZLinkRelocationEnvelopeCodec
{
    private static readonly UTF8Encoding StrictUtf8 = new(false, true);
    private const uint Magic = 0x5a4c5231; // ZLR1
    private const ushort Version = 2;
    private const int MaxFieldBytes = 64 * 1024 * 1024;
    private const int MaxParticipants = 1024;
    private const int MaxItemsPerParticipant = 65_536;

    internal static byte[] Encode(ZLinkRelocationEnvelope envelope)
    {
        using var stream = new MemoryStream();
        EncodeTo(stream, envelope);
        return stream.ToArray();
    }

    internal static long MeasureEncodedLength(ZLinkRelocationEnvelope envelope)
    {
        using var stream = new CountingStream();
        EncodeTo(stream, envelope);
        return stream.Length;
    }

    internal static byte[] ComputeEncodedSha256(ZLinkRelocationEnvelope envelope)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        using var stream = new HashStream(hash);
        EncodeTo(stream, envelope);
        return hash.GetHashAndReset();
    }

    internal static void EncodeTo(
        Stream stream,
        ZLinkRelocationEnvelope envelope)
    {
        ArgumentNullException.ThrowIfNull(stream);
        ArgumentNullException.ThrowIfNull(envelope);
        if (!envelope.CanonicalLogicalStream.IsEmpty)
        {
            stream.Write(envelope.CanonicalLogicalStream.Span);
            return;
        }
        ValidateEnvelope(envelope);
        using var writer = new BinaryWriter(stream, Encoding.UTF8, leaveOpen: true);
        writer.Write(Magic);
        writer.Write(Version);
        writer.Write(envelope.AggregateId.ToByteArray());
        writer.Write(envelope.AggregateGeneration);
        WriteBytes(writer, envelope.InventoryDigest.Span);
        writer.Write(envelope.Participants.Count);
        foreach (var participant in envelope.Participants)
        {
            WriteString(writer, participant.AuthorityKey.Value);
            writer.Write((byte)participant.ObjectKind);
            writer.Write(participant.ObjectGeneration);
            writer.Write(participant.AuthorityOwnerGeneration);
            WriteBytes(writer, participant.ApplicationState.Span);
            writer.Write(participant.AcceptedJobs.Count);
            foreach (var job in participant.AcceptedJobs)
            {
                writer.Write(job.AcceptedSequence);
                WriteBytes(writer, job.Payload.Span);
            }
            writer.Write(participant.LogicalTimers.Count);
            foreach (var timer in participant.LogicalTimers)
            {
                WriteString(writer, timer.TimerId);
                writer.Write(timer.DueUnixTimeMilliseconds);
                writer.Write(timer.PeriodMilliseconds);
                WriteBytes(writer, timer.Payload.Span);
            }
            WriteBytes(writer, participant.RecoveryPayload.Span);
            WriteBytes(writer, participant.CompletionPayload.Span);
        }
        writer.Flush();
    }

    internal static ZLinkRelocationEnvelope Decode(ReadOnlySpan<byte> encoded)
    {
        using var stream = new MemoryStream(encoded.ToArray(), writable: false);
        return Decode(stream);
    }

    internal static ZLinkRelocationEnvelope Decode(
        Stream stream,
        ReadOnlyMemory<byte> inventoryDigest = default)
    {
        ArgumentNullException.ThrowIfNull(stream);
        using var encoded = new MemoryStream();
        stream.CopyTo(encoded);
        var bytes = encoded.ToArray();
        if (bytes.Length < sizeof(uint))
            throw new InvalidDataException("The relocation logical stream is truncated.");
        if (BinaryPrimitives.ReadUInt32LittleEndian(bytes) != Magic)
            return DecodeCanonical(bytes, inventoryDigest);

        using var input = new MemoryStream(bytes, writable: false);
        using var reader = new BinaryReader(input, Encoding.UTF8, leaveOpen: true);
        if (reader.ReadUInt32() != Magic || reader.ReadUInt16() != Version)
            throw new InvalidDataException("The relocation root header is invalid.");
        var aggregateId = new Guid(ReadExact(reader, 16));
        var aggregateGeneration = reader.ReadUInt64();
        var decodedInventoryDigest = ReadBytes(reader, 32);
        if (decodedInventoryDigest.Length != 32)
            throw new InvalidDataException(
                "The relocation inventory digest must contain 32 bytes.");
        var participantCount = ReadCount(reader, MaxParticipants, "participant");
        var participants = new ZLinkRelocationParticipantEnvelope[participantCount];
        for (var participantIndex = 0; participantIndex < participantCount; participantIndex++)
        {
            var key = new ZLinkAuthorityKey(ReadString(reader));
            var objectKind = (ZLinkPlacementObjectKind)reader.ReadByte();
            if (!Enum.IsDefined(objectKind))
                throw new InvalidDataException("The relocation object kind is invalid.");
            var objectGeneration = reader.ReadUInt64();
            var ownerGeneration = reader.ReadUInt64();
            if (objectGeneration == 0 || ownerGeneration == 0)
                throw new InvalidDataException(
                    "Relocation participant generations must be non-zero.");
            var state = ReadBytes(reader, MaxFieldBytes);
            var jobs = new ZLinkRelocationQueuedJob[
                ReadCount(reader, MaxItemsPerParticipant, "accepted job")];
            ulong previousSequence = 0;
            for (var jobIndex = 0; jobIndex < jobs.Length; jobIndex++)
            {
                var sequence = reader.ReadUInt64();
                if (sequence == 0 || sequence <= previousSequence)
                    throw new InvalidDataException(
                        "Accepted job sequences must be strictly increasing.");
                previousSequence = sequence;
                jobs[jobIndex] = new ZLinkRelocationQueuedJob(
                    sequence,
                    ReadBytes(reader, MaxFieldBytes));
            }
            var timers = new ZLinkRelocationLogicalTimer[
                ReadCount(reader, MaxItemsPerParticipant, "logical timer")];
            var timerIds = new HashSet<string>(StringComparer.Ordinal);
            for (var timerIndex = 0; timerIndex < timers.Length; timerIndex++)
            {
                var timerId = ReadString(reader);
                if (!timerIds.Add(timerId))
                    throw new InvalidDataException(
                        $"Duplicate logical timer '{timerId}'.");
                var due = reader.ReadInt64();
                var period = reader.ReadInt64();
                if (period < 0)
                    throw new InvalidDataException(
                        "A logical timer period cannot be negative.");
                timers[timerIndex] = new ZLinkRelocationLogicalTimer(
                    timerId,
                    due,
                    period,
                    ReadBytes(reader, MaxFieldBytes));
            }
            participants[participantIndex] = new ZLinkRelocationParticipantEnvelope(
                key,
                objectKind,
                objectGeneration,
                ownerGeneration,
                state,
                jobs,
                timers,
                ReadBytes(reader, MaxFieldBytes),
                ReadBytes(reader, MaxFieldBytes));
        }
        if (input.ReadByte() != -1)
            throw new InvalidDataException(
                "The relocation root contains trailing bytes.");

        var envelope = new ZLinkRelocationEnvelope(
            aggregateId,
            aggregateGeneration,
            decodedInventoryDigest,
            participants);
        ValidateEnvelope(envelope);
        return envelope;
    }

    private static ZLinkRelocationEnvelope DecodeCanonical(
        ReadOnlyMemory<byte> encoded,
        ReadOnlyMemory<byte> inventoryDigest)
    {
        var reader = new CanonicalReader(encoded.Span);
        var relocationId = reader.ReadBytes(16);
        if (relocationId.IndexOfAnyExcept((byte)0) < 0)
            throw new InvalidDataException("The relocation id must not be zero.");

        var objectKindValue = reader.ReadByte();
        if (objectKindValue is < 1 or > 3)
            throw new InvalidDataException("The relocation object kind is invalid.");
        var objectBody = new CanonicalReader(reader.ReadBytes(reader.ReadUInt16()));
        string authorityKey;
        ulong objectGeneration;
        ulong ownerGeneration;
        if (objectKindValue is 1 or 2)
        {
            authorityKey = objectBody.ReadText8();
            objectGeneration = objectBody.ReadUInt64();
            ownerGeneration = objectBody.ReadUInt64();
        }
        else
        {
            _ = objectBody.ReadText8(); // Instance stable type.
            authorityKey = objectBody.ReadText8();
            objectGeneration = objectBody.ReadUInt64();
            ownerGeneration = 1;
        }
        objectBody.RequireEnd("relocation object identity");
        if (objectGeneration == 0 || ownerGeneration == 0)
            throw new InvalidDataException(
                "Relocation participant generations must be non-zero.");

        var applicationVersion = reader.ReadInt64();
        if (applicationVersion < 0)
            throw new InvalidDataException("The application version is invalid.");

        var states = new Dictionary<ulong, ReadOnlyMemory<byte>>();
        var stateCount = reader.ReadUInt32();
        if (stateCount is 0 or > MaxParticipants)
            throw new InvalidDataException(
                "The relocation application-state count exceeds its bound.");
        ulong previousParticipantId = 0;
        for (var index = 0; index < stateCount; index++)
        {
            var participantId = reader.ReadUInt64();
            if (participantId == 0 || participantId <= previousParticipantId)
                throw new InvalidDataException(
                    "Relocation participant ids must be strictly increasing.");
            previousParticipantId = participantId;
            var hasState = reader.ReadByte();
            if (hasState > 1)
                throw new InvalidDataException("The relocation state discriminator is invalid.");
            var body = new CanonicalReader(reader.ReadBytes(reader.ReadUInt64AsInt()));
            ReadOnlyMemory<byte> state = default;
            if (hasState == 1)
                state = body.ReadBytes(body.ReadUInt64AsInt()).ToArray();
            body.RequireEnd("relocation application state");
            states.Add(participantId, state);
        }

        var progressCount = reader.ReadUInt32();
        if (progressCount != stateCount)
            throw new InvalidDataException(
                "Relocation progress must cover the application-state participants.");
        previousParticipantId = 0;
        var progress = new Dictionary<ulong, (ulong AcceptedBoundary, ulong ReplayCursor)>();
        for (var index = 0; index < progressCount; index++)
        {
            var participantId = reader.ReadUInt64();
            var acceptedBoundary = reader.ReadUInt64();
            var replayCursor = reader.ReadUInt64();
            if (participantId == 0 || participantId <= previousParticipantId
                || replayCursor > acceptedBoundary || !states.ContainsKey(participantId))
                throw new InvalidDataException("The relocation participant progress is invalid.");
            previousParticipantId = participantId;
            progress.Add(participantId, (acceptedBoundary, replayCursor));
        }

        var jobs = ReadCanonicalJournal(ref reader, progress);
        var timers = ReadCanonicalTimers(ref reader, states.Keys);
        ReadCanonicalPendingTimerTicks(ref reader, states.Keys, timers);
        var completions = ReadCanonicalCompletions(ref reader, states.Keys);
        reader.RequireEnd("relocation root");

        var participants = states.Select((entry, index) =>
            new ZLinkRelocationParticipantEnvelope(
                new ZLinkAuthorityKey(index == 0
                    ? authorityKey
                    : $"{authorityKey}#participant:{entry.Key}"),
                (ZLinkPlacementObjectKind)objectKindValue,
                objectGeneration,
                ownerGeneration,
                entry.Value,
                jobs.GetValueOrDefault(entry.Key, []),
                timers.GetValueOrDefault(entry.Key, [])
                    .Select(static timer => timer.ToRelocationTimer())
                    .ToArray(),
                CompletionPayload: completions.GetValueOrDefault(entry.Key)))
            .ToArray();
        var digest = inventoryDigest.IsEmpty
            ? new byte[32]
            : inventoryDigest.ToArray();
        if (digest.Length != 32)
            throw new InvalidDataException(
                "The relocation inventory digest must contain 32 bytes.");
        var result = new ZLinkRelocationEnvelope(
            new Guid(relocationId),
            applicationVersion == 0 ? 1UL : checked((ulong)applicationVersion),
            digest,
            participants)
        {
            CanonicalLogicalStream = encoded.ToArray()
        };
        ValidateEnvelope(result);
        return result;
    }

    private static Dictionary<ulong, IReadOnlyList<ZLinkRelocationQueuedJob>>
        ReadCanonicalJournal(
            ref CanonicalReader reader,
            IReadOnlyDictionary<ulong, (ulong AcceptedBoundary, ulong ReplayCursor)> progress)
    {
        var jobs = progress.Keys.ToDictionary(
            static id => id,
            static _ => (IReadOnlyList<ZLinkRelocationQueuedJob>)
                new List<ZLinkRelocationQueuedJob>());
        var count = reader.ReadBoundedCount(MaxItemsPerParticipant, "journal");
        ulong previousParticipantId = 0;
        ulong previousSequence = 0;
        for (var index = 0; index < count; index++)
        {
            var participantId = reader.ReadUInt64();
            var sequence = reader.ReadUInt64();
            if (!jobs.TryGetValue(participantId, out var participantJobs)
                || participantId < previousParticipantId
                || participantId == previousParticipantId && sequence <= previousSequence
                || sequence == 0
                || sequence > progress[participantId].AcceptedBoundary
                || sequence <= progress[participantId].ReplayCursor)
                throw new InvalidDataException("The relocation journal order is invalid.");
            previousSequence = participantId == previousParticipantId ? sequence : 0;
            previousParticipantId = participantId;
            previousSequence = sequence;
            var recordStart = reader.Position;
            ReadCanonicalFrozenRecord(ref reader);
            ((List<ZLinkRelocationQueuedJob>)participantJobs).Add(
                new ZLinkRelocationQueuedJob(
                    sequence,
                    reader.CopyRange(recordStart)));
        }
        return jobs;
    }

    private static Dictionary<ulong, IReadOnlyList<CanonicalTimerProjection>>
        ReadCanonicalTimers(
            ref CanonicalReader reader,
            ICollection<ulong> participantIds)
    {
        var timers = participantIds.ToDictionary(
            static id => id,
            static _ => (IReadOnlyList<CanonicalTimerProjection>)
                new List<CanonicalTimerProjection>());
        var count = reader.ReadBoundedCount(MaxItemsPerParticipant, "timer registration");
        ulong previousParticipantId = 0;
        string? previousName = null;
        for (var index = 0; index < count; index++)
        {
            var start = reader.Position;
            var participantId = reader.ReadUInt64();
            var name = reader.ReadText8();
            _ = reader.ReadText8(); // Handler type is retained in the opaque projection.
            var period = reader.ReadUInt64();
            var policy = reader.ReadByte();
            var maxCatchUpTicks = reader.ReadUInt64();
            var stopOnUnhandledException = reader.ReadByte();
            _ = reader.ReadUInt64();
            _ = reader.ReadUInt64();
            var nextScheduledAt = reader.ReadUInt64();
            if (!timers.TryGetValue(participantId, out var participantTimers)
                || participantId < previousParticipantId
                || participantId == previousParticipantId
                   && string.CompareOrdinal(name, previousName) <= 0
                || period == 0
                || policy is < 1 or > 3
                || maxCatchUpTicks == 0
                || stopOnUnhandledException > 1
                || nextScheduledAt > long.MaxValue)
                throw new InvalidDataException(
                    "The relocation timer registration is invalid.");
            previousParticipantId = participantId;
            previousName = name;
            ((List<CanonicalTimerProjection>)participantTimers).Add(
                new CanonicalTimerProjection(
                    name,
                    checked((long)nextScheduledAt),
                    checked((long)period),
                    reader.CopyRange(start)));
        }
        return timers;
    }

    private static void ReadCanonicalPendingTimerTicks(
        ref CanonicalReader reader,
        ICollection<ulong> participantIds,
        Dictionary<ulong, IReadOnlyList<CanonicalTimerProjection>> timers)
    {
        var count = reader.ReadBoundedCount(MaxItemsPerParticipant, "pending timer tick");
        ulong previousParticipantId = 0;
        ulong previousSequence = 0;
        for (var index = 0; index < count; index++)
        {
            var start = reader.Position;
            var participantId = reader.ReadUInt64();
            var sequence = reader.ReadUInt64();
            var timerName = reader.ReadText8();
            var deliveryIndex = reader.ReadUInt64();
            var scheduledIndex = reader.ReadUInt64();
            _ = reader.ReadUInt64();
            _ = reader.ReadUInt64();
            if (!participantIds.Contains(participantId)
                || participantId < previousParticipantId
                || participantId == previousParticipantId && sequence <= previousSequence
                || sequence == 0 || deliveryIndex == 0 || scheduledIndex == 0
                || !timers[participantId].Any(timer =>
                    string.Equals(timer.TimerId, timerName, StringComparison.Ordinal)))
                throw new InvalidDataException("The pending relocation timer tick is invalid.");
            previousSequence = participantId == previousParticipantId ? sequence : 0;
            previousParticipantId = participantId;
            previousSequence = sequence;
            var timer = timers[participantId].Single(candidate =>
                string.Equals(candidate.TimerId, timerName, StringComparison.Ordinal));
            timer.AppendPendingTick(reader.CopyRange(start));
        }
    }

    private static Dictionary<ulong, ReadOnlyMemory<byte>> ReadCanonicalCompletions(
        ref CanonicalReader reader,
        ICollection<ulong> participantIds)
    {
        var records = participantIds.ToDictionary(
            static id => id,
            static _ => new List<byte[]>());
        var count = reader.ReadBoundedCount(MaxItemsPerParticipant, "terminal completion");
        var operations = new HashSet<(string OwnerId, ulong OwnerLease, string NodeRid,
            ulong NodeGeneration, ulong OperationHigh, ulong OperationLow)>();
        ulong previousParticipantId = 0;
        ulong previousSequence = 0;
        for (var index = 0; index < count; index++)
        {
            var start = reader.Position;
            var operationHigh = reader.ReadUInt64();
            var operationLow = reader.ReadUInt64();
            var sourceOwnerId = reader.ReadText8();
            var sourceOwnerLease = reader.ReadUInt64();
            if (sourceOwnerLease == 0)
                throw new InvalidDataException("The completion source lease is invalid.");
            var sourceNodeRid = reader.ReadText8();
            var sourceNodeGeneration = reader.ReadUInt64();
            if (sourceNodeGeneration == 0)
                throw new InvalidDataException("The completion source node generation is invalid.");
            var participantId = reader.ReadUInt64();
            var sequence = reader.ReadUInt64();
            var terminalResult = reader.ReadUInt32();
            _ = reader.ReadUInt32();
            var deliveryState = reader.ReadByte();
            var hasPayload = reader.ReadByte();
            if (!records.ContainsKey(participantId)
                || sequence == 0
                || participantId < previousParticipantId
                || participantId == previousParticipantId && sequence <= previousSequence
                || operationHigh == 0 && operationLow == 0
                || !operations.Add((
                    sourceOwnerId,
                    sourceOwnerLease,
                    sourceNodeRid,
                    sourceNodeGeneration,
                    operationHigh,
                    operationLow))
                || !IsCanonicalTerminalResult(terminalResult)
                || deliveryState > 3
                || hasPayload > 1)
                throw new InvalidDataException("The relocation terminal completion is invalid.");
            previousParticipantId = participantId;
            previousSequence = sequence;
            if (hasPayload == 1)
                ReadCanonicalApplicationPayload(ref reader);
            records[participantId].Add(reader.CopyRange(start));
        }
        return records.ToDictionary(
            static pair => pair.Key,
            static pair => (ReadOnlyMemory<byte>)JoinCanonicalRecords(pair.Value));
    }

    private static void ReadCanonicalFrozenRecord(ref CanonicalReader reader)
    {
        var recordKind = reader.ReadByte();
        if (recordKind is < 1 or > 14)
            throw new InvalidDataException("The relocation journal record kind is invalid.");
        var sourceKind = reader.ReadByte();
        if (sourceKind is < 1 or > 4)
            throw new InvalidDataException("The relocation journal source kind is invalid.");
        var source = new CanonicalReader(reader.ReadBytes(reader.ReadUInt16()));
        _ = source.ReadText8();
        if (source.ReadUInt64() == 0)
            throw new InvalidDataException("The relocation journal source generation is invalid.");
        _ = source.ReadText8();
        if (source.ReadUInt64() == 0)
            throw new InvalidDataException("The relocation journal source lease is invalid.");
        if (sourceKind == 2)
            _ = source.ReadText8();
        else if (sourceKind is 3 or 4)
        {
            _ = source.ReadText8();
            if (source.ReadUInt64() == 0)
                throw new InvalidDataException("The relocation journal Actor generation is invalid.");
            if (sourceKind == 4)
            {
                _ = source.ReadText8();
                if (source.ReadUInt64() == 0 || source.ReadUInt64() == 0)
                    throw new InvalidDataException("The relocation journal binding is invalid.");
            }
        }
        source.RequireEnd("relocation journal source");
        var hasMetadata = reader.ReadByte();
        if (hasMetadata > 1)
            throw new InvalidDataException("The relocation journal metadata flag is invalid.");
        if (hasMetadata == 1)
            ReadCanonicalMetadata(ref reader);
        _ = reader.ReadUInt64();
        _ = reader.ReadUInt64();
        var operationKind = reader.ReadUInt32();
        if (operationKind > 12)
            throw new InvalidDataException("The relocation journal operation kind is invalid.");
        var replyRoute = new CanonicalReader(reader.ReadBytes(reader.ReadUInt16()));
        if (operationKind is 1 or 2 or 3 or 4 or 12)
        {
            if (replyRoute.ReadUInt64() == 0)
                throw new InvalidDataException("The relocation reply route is invalid.");
        }
        replyRoute.RequireEnd("relocation reply route");
        ReadCanonicalFrozenRecordBody(ref reader, recordKind);
    }

    private static void ReadCanonicalFrozenRecordBody(
        ref CanonicalReader reader,
        byte recordKind)
    {
        switch (recordKind)
        {
            case 1 or 2:
                ReadCanonicalApplicationPayload(ref reader);
                return;
            case 3 or 4:
                _ = reader.ReadText8();
                ReadCanonicalApplicationPayload(ref reader);
                return;
            case 5 or 6:
                ReadCanonicalSpotFence(ref reader);
                ReadCanonicalApplicationPayload(ref reader);
                return;
            case 7:
                _ = reader.ReadText8();
                _ = reader.ReadText8();
                ReadCanonicalApplicationPayload(ref reader);
                return;
            case 8:
                ReadCanonicalActorControl(ref reader);
                return;
            case 9 or 10:
                ReadCanonicalActorFence(ref reader);
                ReadCanonicalApplicationPayload(ref reader);
                return;
            case 11:
                if (!IsCanonicalTerminalResult(reader.ReadUInt32()))
                    throw new InvalidDataException("The journal terminal result is invalid.");
                _ = reader.ReadUInt32();
                var hasPayload = reader.ReadByte();
                if (hasPayload > 1)
                    throw new InvalidDataException("The journal payload flag is invalid.");
                if (hasPayload == 1)
                    ReadCanonicalApplicationPayload(ref reader);
                return;
            case 12:
                ReadCanonicalSendReadyDestination(ref reader);
                return;
            case 13:
                ReadCanonicalRelocationControl(ref reader);
                return;
            case 14:
                ReadCanonicalInstanceRoute(ref reader);
                if (reader.ReadUInt64() == 0 || reader.ReadByte() is < 1 or > 2)
                    throw new InvalidDataException("The Instance activation record is invalid.");
                ReadCanonicalApplicationPayload(ref reader);
                return;
            default:
                throw new InvalidDataException("The relocation journal record kind is invalid.");
        }
    }

    private static void ReadCanonicalActorControl(ref CanonicalReader reader)
    {
        var lifecycle = reader.ReadByte();
        if (lifecycle is < 1 or > 5)
            throw new InvalidDataException("The Actor lifecycle kind is invalid.");
        var body = new CanonicalReader(reader.ReadBytes(reader.ReadUInt16()));
        if (lifecycle == 2)
        {
            ReadCanonicalOptionalActorMembership(ref body);
            ReadCanonicalActorMembership(ref body);
        }
        else if (lifecycle == 3)
        {
            ReadCanonicalActorMembership(ref body);
            ReadCanonicalActorMembership(ref body);
        }
        else
        {
            ReadCanonicalActorMembership(ref body);
        }
        body.RequireEnd("Actor lifecycle control");
    }

    private static void ReadCanonicalOptionalActorMembership(ref CanonicalReader reader)
    {
        var present = reader.ReadByte();
        if (present > 1)
            throw new InvalidDataException("The Actor membership presence flag is invalid.");
        var body = new CanonicalReader(reader.ReadBytes(reader.ReadUInt16()));
        if (present == 1)
            ReadCanonicalActorMembership(ref body);
        body.RequireEnd("optional Actor membership");
    }

    private static void ReadCanonicalActorMembership(ref CanonicalReader reader)
    {
        ReadCanonicalActorRef(ref reader);
        ReadCanonicalSpotRef(ref reader);
    }

    private static void ReadCanonicalActorRef(ref CanonicalReader reader)
    {
        _ = reader.ReadText8();
        if (reader.ReadUInt64() == 0)
            throw new InvalidDataException("The Actor generation is invalid.");
    }

    private static void ReadCanonicalSpotRef(ref CanonicalReader reader)
    {
        _ = reader.ReadText8();
        if (reader.ReadUInt64() == 0)
            throw new InvalidDataException("The Spot generation is invalid.");
    }

    private static void ReadCanonicalActorFence(ref CanonicalReader reader)
    {
        ReadCanonicalActorRef(ref reader);
        _ = reader.ReadText8();
        if (reader.ReadUInt64() == 0
            || reader.ReadUInt64() == 0
            || reader.ReadUInt64() == 0)
            throw new InvalidDataException("The Actor authority fence is invalid.");
    }

    private static void ReadCanonicalSendReadyDestination(ref CanonicalReader reader)
    {
        var kind = reader.ReadByte();
        if (kind is < 1 or > 5)
            throw new InvalidDataException("The send-ready destination kind is invalid.");
        var body = new CanonicalReader(reader.ReadBytes(reader.ReadUInt16()));
        switch (kind)
        {
            case 1 or 2:
                _ = body.ReadText8();
                break;
            case 3:
                ReadCanonicalSpotFence(ref body);
                break;
            case 4:
                ReadCanonicalActorFence(ref body);
                break;
            case 5:
                ReadCanonicalActorFence(ref body);
                if (body.ReadUInt64() == 0)
                    throw new InvalidDataException("The session binding generation is invalid.");
                break;
        }
        body.RequireEnd("send-ready destination");
    }

    private static void ReadCanonicalRelocationControl(ref CanonicalReader reader)
    {
        if (reader.ReadByte() > 9 || reader.ReadByte() is < 1 or > 3)
            throw new InvalidDataException("The relocation control discriminator is invalid.");
        var relocationId = reader.ReadBytes(16);
        if (relocationId.IndexOfAnyExcept((byte)0) < 0)
            throw new InvalidDataException("The relocation control id is invalid.");
        ReadCanonicalRelocationObject(ref reader);
        if (!IsCanonicalTerminalResult(reader.ReadUInt32()))
            throw new InvalidDataException("The relocation control result is invalid.");
        _ = reader.ReadUInt32();
    }

    private static void ReadCanonicalRelocationObject(ref CanonicalReader reader)
    {
        var kind = reader.ReadByte();
        if (kind is < 1 or > 3)
            throw new InvalidDataException("The relocation object kind is invalid.");
        var body = new CanonicalReader(reader.ReadBytes(reader.ReadUInt16()));
        if (kind == 1)
        {
            ReadCanonicalActorRef(ref body);
            if (body.ReadUInt64() == 0)
                throw new InvalidDataException("The Actor owner generation is invalid.");
        }
        else if (kind == 2)
        {
            ReadCanonicalSpotRef(ref body);
            if (body.ReadUInt64() == 0)
                throw new InvalidDataException("The Spot owner generation is invalid.");
        }
        else
        {
            _ = body.ReadText8();
            _ = body.ReadText8();
            if (body.ReadUInt64() == 0)
                throw new InvalidDataException("The Instance Spot generation is invalid.");
        }
        body.RequireEnd("relocation object identity");
    }

    private static void ReadCanonicalSpotFence(ref CanonicalReader reader)
    {
        _ = reader.ReadText8();
        if (reader.ReadUInt64() == 0)
            throw new InvalidDataException("The relocation Spot generation is invalid.");
        _ = reader.ReadText8();
        if (reader.ReadUInt64() == 0 || reader.ReadUInt64() == 0)
            throw new InvalidDataException("The relocation Spot authority fence is invalid.");
    }

    private static void ReadCanonicalInstanceRoute(ref CanonicalReader reader)
    {
        if (reader.ReadByte() != 1)
            throw new InvalidDataException("The Instance relocation route version is invalid.");
        var route = new CanonicalReader(reader.ReadBytes(reader.ReadUInt16()));
        _ = route.ReadText8();
        if (route.ReadUInt64() == 0)
            throw new InvalidDataException("The Instance relocation node generation is invalid.");
        _ = route.ReadText8();
        if (route.ReadUInt64() == 0)
            throw new InvalidDataException("The Instance relocation object generation is invalid.");
        _ = route.ReadText8();
        if (route.ReadUInt64() == 0 || route.ReadUInt64() == 0)
            throw new InvalidDataException("The Instance relocation authority fence is invalid.");
        _ = route.ReadText16();
        _ = route.ReadText8();
        route.RequireEnd("Instance relocation route");
    }

    private static void ReadCanonicalApplicationPayload(ref CanonicalReader reader)
    {
        if (reader.ReadByte() != 1)
            throw new InvalidDataException("The relocation application payload version is invalid.");
        var body = new CanonicalReader(reader.ReadBytes(checked((int)reader.ReadUInt32())));
        _ = body.ReadText8();
        _ = body.ReadText8();
        _ = body.ReadBytes(checked((int)body.ReadUInt32()));
        body.RequireEnd("relocation application payload");
    }

    private static void ReadCanonicalMetadata(ref CanonicalReader reader)
    {
        if (reader.ReadByte() != 1)
            throw new InvalidDataException("The relocation metadata version is invalid.");
        var count = reader.ReadByte();
        var keys = new HashSet<string>(StringComparer.Ordinal);
        for (var index = 0; index < count; index++)
        {
            if (!keys.Add(reader.ReadText8()))
                throw new InvalidDataException("The relocation metadata contains a duplicate key.");
            _ = reader.ReadText16();
        }
    }

    private static bool IsCanonicalTerminalResult(uint value) =>
        value == 0 || value is >= 101 and <= 113;

    private static byte[] JoinCanonicalRecords(IReadOnlyList<byte[]> records)
    {
        var length = records.Sum(static record => sizeof(int) + record.Length);
        var result = new byte[length];
        var offset = 0;
        foreach (var record in records)
        {
            BinaryPrimitives.WriteInt32BigEndian(result.AsSpan(offset), record.Length);
            offset += sizeof(int);
            record.CopyTo(result, offset);
            offset += record.Length;
        }
        return result;
    }

    private sealed class CanonicalTimerProjection(
        string timerId,
        long dueUnixTimeMilliseconds,
        long periodMilliseconds,
        byte[] registration)
    {
        private readonly List<byte[]> _records = [registration];

        internal string TimerId { get; } = timerId;

        internal void AppendPendingTick(byte[] tick) => _records.Add(tick);

        internal ZLinkRelocationLogicalTimer ToRelocationTimer() =>
            new(
                TimerId,
                dueUnixTimeMilliseconds,
                periodMilliseconds,
                JoinCanonicalRecords(_records));
    }

    private ref struct CanonicalReader(ReadOnlySpan<byte> source)
    {
        private readonly ReadOnlySpan<byte> _source = source;
        private int _offset;

        internal int Position => _offset;

        internal byte ReadByte() => ReadBytes(1)[0];

        internal ushort ReadUInt16() =>
            BinaryPrimitives.ReadUInt16BigEndian(ReadBytes(sizeof(ushort)));

        internal uint ReadUInt32() =>
            BinaryPrimitives.ReadUInt32BigEndian(ReadBytes(sizeof(uint)));

        internal ulong ReadUInt64() =>
            BinaryPrimitives.ReadUInt64BigEndian(ReadBytes(sizeof(ulong)));

        internal long ReadInt64() =>
            BinaryPrimitives.ReadInt64BigEndian(ReadBytes(sizeof(long)));

        internal int ReadUInt64AsInt()
        {
            var value = ReadUInt64();
            if (value > int.MaxValue)
                throw new InvalidDataException(
                    "The relocation field is too large for the runtime projection.");
            return checked((int)value);
        }

        internal string ReadText8()
        {
            var length = ReadByte();
            if (length == 0)
                throw new InvalidDataException("A relocation string is empty.");
            return DecodeText(ReadBytes(length));
        }

        internal string ReadText16()
        {
            var length = ReadUInt16();
            if (length == 0)
                throw new InvalidDataException("A relocation string is empty.");
            return DecodeText(ReadBytes(length));
        }

        private static string DecodeText(ReadOnlySpan<byte> encoded)
        {
            try
            {
                return StrictUtf8.GetString(encoded);
            }
            catch (DecoderFallbackException error)
            {
                throw new InvalidDataException(
                    "A relocation string contains invalid UTF-8.",
                    error);
            }
        }

        internal int ReadBoundedCount(int maximum, string field)
        {
            var count = ReadUInt32();
            if (count > maximum)
                throw new InvalidDataException(
                    $"The relocation {field} count exceeds its bound.");
            return checked((int)count);
        }

        internal byte[] CopyRange(int start)
        {
            if (start < 0 || start > _offset)
                throw new ArgumentOutOfRangeException(nameof(start));
            return _source.Slice(start, _offset - start).ToArray();
        }

        internal ReadOnlySpan<byte> ReadBytes(int length)
        {
            if (length < 0 || length > _source.Length - _offset)
                throw new EndOfStreamException();
            var value = _source.Slice(_offset, length);
            _offset += length;
            return value;
        }

        internal void RequireEnd(string field)
        {
            if (_offset != _source.Length)
                throw new InvalidDataException($"The {field} contains trailing bytes.");
        }
    }

    private static void ValidateEnvelope(ZLinkRelocationEnvelope envelope)
    {
        if (envelope.AggregateId == Guid.Empty)
            throw new ArgumentException(
                "A relocation aggregate id must not be empty.",
                nameof(envelope));
        if (envelope.AggregateGeneration is 0 or > long.MaxValue)
            throw new ArgumentOutOfRangeException(
                nameof(envelope),
                "A relocation aggregate generation must be a non-zero signed 63-bit value.");
        if (envelope.InventoryDigest.Length != 32)
            throw new ArgumentException(
                "A relocation inventory digest must contain 32 bytes.",
                nameof(envelope));
        if (envelope.Participants.Count is < 1 or > MaxParticipants)
            throw new ArgumentOutOfRangeException(
                nameof(envelope),
                "A relocation aggregate must contain 1 to 1024 participants.");

        var keys = new HashSet<string>(StringComparer.Ordinal);
        foreach (var participant in envelope.Participants)
        {
            ArgumentException.ThrowIfNullOrWhiteSpace(participant.AuthorityKey.Value);
            if (!keys.Add(participant.AuthorityKey.Value))
                throw new ArgumentException(
                    $"Duplicate relocation participant '{participant.AuthorityKey.Value}'.",
                    nameof(envelope));
            if (participant.ObjectGeneration is 0 or > long.MaxValue
                || participant.AuthorityOwnerGeneration is 0 or > long.MaxValue)
                throw new ArgumentOutOfRangeException(
                    nameof(envelope),
                    "Relocation participant generations must be non-zero signed 63-bit values.");
            if (participant.AcceptedJobs.Count > MaxItemsPerParticipant
                || participant.LogicalTimers.Count > MaxItemsPerParticipant)
                throw new ArgumentOutOfRangeException(
                    nameof(envelope),
                    "A relocation participant contains too many jobs or timers.");

            ulong previousSequence = 0;
            foreach (var job in participant.AcceptedJobs)
            {
                if (job.AcceptedSequence == 0
                    || job.AcceptedSequence <= previousSequence)
                    throw new ArgumentException(
                        "Accepted job sequences must be strictly increasing.",
                        nameof(envelope));
                previousSequence = job.AcceptedSequence;
            }
            var timerIds = new HashSet<string>(StringComparer.Ordinal);
            foreach (var timer in participant.LogicalTimers)
            {
                ArgumentException.ThrowIfNullOrWhiteSpace(timer.TimerId);
                if (!timerIds.Add(timer.TimerId))
                    throw new ArgumentException(
                        $"Duplicate logical timer '{timer.TimerId}'.",
                        nameof(envelope));
                if (timer.PeriodMilliseconds < 0)
                    throw new ArgumentOutOfRangeException(
                        nameof(envelope),
                        "A logical timer period cannot be negative.");
            }
        }
    }

    private static void WriteString(BinaryWriter writer, string value)
    {
        var encoded = Encoding.UTF8.GetBytes(value);
        if (encoded.Length is < 1 or > ushort.MaxValue)
            throw new ArgumentOutOfRangeException(
                nameof(value),
                "Relocation strings must be 1 to 65535 UTF-8 bytes.");
        writer.Write((ushort)encoded.Length);
        writer.Write(encoded);
    }

    private static string ReadString(BinaryReader reader)
    {
        var size = reader.ReadUInt16();
        if (size == 0)
            throw new InvalidDataException("A relocation string is empty.");
        return Encoding.UTF8.GetString(ReadExact(reader, size));
    }

    private static void WriteBytes(BinaryWriter writer, ReadOnlySpan<byte> value)
    {
        writer.Write(value.Length);
        writer.Write(value);
    }

    private static byte[] ReadBytes(BinaryReader reader, int maximum)
    {
        var size = reader.ReadInt32();
        if (size < 0 || size > maximum)
            throw new InvalidDataException(
                "A relocation byte field exceeds its bound.");
        return ReadExact(reader, size);
    }

    private static int ReadCount(BinaryReader reader, int maximum, string name)
    {
        var count = reader.ReadInt32();
        if (count < 0 || count > maximum)
            throw new InvalidDataException(
                $"The relocation {name} count exceeds its bound.");
        return count;
    }

    private static byte[] ReadExact(BinaryReader reader, int size)
    {
        var value = reader.ReadBytes(size);
        if (value.Length != size)
            throw new EndOfStreamException();
        return value;
    }

    private sealed class CountingStream : Stream
    {
        private long _length;

        public override bool CanRead => false;
        public override bool CanSeek => false;
        public override bool CanWrite => true;
        public override long Length => _length;
        public override long Position
        {
            get => Length;
            set => throw new NotSupportedException();
        }

        public override void Write(byte[] buffer, int offset, int count) =>
            _length = checked(_length + count);

        public override void Write(ReadOnlySpan<byte> buffer) =>
            _length = checked(_length + buffer.Length);

        public override void Flush() { }
        public override int Read(byte[] buffer, int offset, int count) =>
            throw new NotSupportedException();
        public override long Seek(long offset, SeekOrigin origin) =>
            throw new NotSupportedException();
        public override void SetLength(long value) =>
            throw new NotSupportedException();
    }

    private sealed class HashStream(IncrementalHash hash) : Stream
    {
        public override bool CanRead => false;
        public override bool CanSeek => false;
        public override bool CanWrite => true;
        public override long Length => throw new NotSupportedException();
        public override long Position
        {
            get => throw new NotSupportedException();
            set => throw new NotSupportedException();
        }

        public override void Write(byte[] buffer, int offset, int count) =>
            hash.AppendData(buffer, offset, count);

        public override void Write(ReadOnlySpan<byte> buffer) =>
            hash.AppendData(buffer);

        public override void Flush() { }
        public override int Read(byte[] buffer, int offset, int count) =>
            throw new NotSupportedException();
        public override long Seek(long offset, SeekOrigin origin) =>
            throw new NotSupportedException();
        public override void SetLength(long value) =>
            throw new NotSupportedException();
    }
}

internal static class ZLinkCrc32C
{
    internal static uint Compute(ReadOnlySpan<byte> data)
    {
        var crc = uint.MaxValue;
        Append(ref crc, data);
        return ~crc;
    }

    internal static void Append(
        ref uint crc,
        ReadOnlySpan<byte> data)
    {
        foreach (var value in data)
        {
            crc ^= value;
            for (var bit = 0; bit < 8; bit++)
                crc = (crc >> 1) ^ (0x82f63b78u & (uint)-(int)(crc & 1));
        }
    }
}
