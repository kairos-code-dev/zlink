using System.Buffers.Binary;
using System.Text;

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
    IReadOnlyList<ZLinkRelocationLogicalTimer> LogicalTimers);

internal sealed record ZLinkRelocationEnvelope(
    Guid AggregateId,
    ulong AggregateGeneration,
    ReadOnlyMemory<byte> InventoryDigest,
    IReadOnlyList<ZLinkRelocationParticipantEnvelope> Participants);

internal static class ZLinkRelocationEnvelopeCodec
{
    private const uint Magic = 0x5a4c5231; // ZLR1
    private const ushort Version = 1;
    private const int MaxRootBytes = 64 * 1024 * 1024;
    private const int MaxParticipants = 1024;
    private const int MaxItemsPerParticipant = 65_536;

    internal static byte[] Encode(ZLinkRelocationEnvelope envelope)
    {
        ArgumentNullException.ThrowIfNull(envelope);
        ValidateEnvelope(envelope);

        using var stream = new MemoryStream();
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
        }
        writer.Flush();
        if (stream.Length > MaxRootBytes)
            throw new InvalidOperationException(
                "A relocation root cannot exceed 64 MiB.");
        return stream.ToArray();
    }

    internal static ZLinkRelocationEnvelope Decode(ReadOnlySpan<byte> encoded)
    {
        if (encoded.Length is <= 0 or > MaxRootBytes)
            throw new InvalidDataException(
                "The relocation root size is outside the 1..64 MiB range.");

        using var stream = new MemoryStream(encoded.ToArray(), writable: false);
        using var reader = new BinaryReader(stream, Encoding.UTF8, leaveOpen: true);
        if (reader.ReadUInt32() != Magic || reader.ReadUInt16() != Version)
            throw new InvalidDataException("The relocation root header is invalid.");
        var aggregateId = new Guid(ReadExact(reader, 16));
        var aggregateGeneration = reader.ReadUInt64();
        var inventoryDigest = ReadBytes(reader, 32);
        if (inventoryDigest.Length != 32)
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
            var state = ReadBytes(reader, MaxRootBytes);
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
                    ReadBytes(reader, MaxRootBytes));
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
                    ReadBytes(reader, MaxRootBytes));
            }
            participants[participantIndex] = new ZLinkRelocationParticipantEnvelope(
                key,
                objectKind,
                objectGeneration,
                ownerGeneration,
                state,
                jobs,
                timers);
        }
        if (stream.Position != stream.Length)
            throw new InvalidDataException(
                "The relocation root contains trailing bytes.");

        var envelope = new ZLinkRelocationEnvelope(
            aggregateId,
            aggregateGeneration,
            inventoryDigest,
            participants);
        ValidateEnvelope(envelope);
        return envelope;
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
}

internal static class ZLinkCrc32C
{
    internal static uint Compute(ReadOnlySpan<byte> data)
    {
        var crc = uint.MaxValue;
        foreach (var value in data)
        {
            crc ^= value;
            for (var bit = 0; bit < 8; bit++)
                crc = (crc >> 1) ^ (0x82f63b78u & (uint)-(int)(crc & 1));
        }
        return ~crc;
    }
}
