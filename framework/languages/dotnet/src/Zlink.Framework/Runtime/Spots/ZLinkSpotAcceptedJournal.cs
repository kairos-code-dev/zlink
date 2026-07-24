using System.Text;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Runtime.Backend.DotNet;

namespace Zlink.Framework.Runtime.Spots;

internal sealed record ZLinkSpotAcceptedJournalRecord(
    RoutingId? SourceNodeRid,
    string? SpotId,
    ulong? RequestSequence,
    ZLinkMessageMetadata Metadata,
    IReadOnlyList<ReadOnlyMemory<byte>> Parts);

internal static class ZLinkSpotAcceptedJournal
{
    private const uint Magic = 0x5a4a5231; // ZJR1
    private const ushort Version = 2;
    private const int MaxRecordBytes = 64 * 1024 * 1024;
    private const int MaxParts = 65_536;

    internal static byte[] Encode(ZLinkBackendRouteReceived received)
    {
        ArgumentNullException.ThrowIfNull(received);
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream, Encoding.UTF8, leaveOpen: true);
        writer.Write(Magic);
        writer.Write(Version);
        WriteRoutingId(writer, received.SourceNodeRid);
        WriteSpotId(writer, received.SpotId);
        writer.Write(received.RequestSeq.HasValue);
        if (received.RequestSeq is { } requestSequence)
            writer.Write(requestSequence);
        WriteBytes(writer, ZLinkMeshMetadataCodec.Encode(received.Metadata).Span);
        if (received.Parts.Count > MaxParts)
            throw new InvalidOperationException(
                "An accepted Spot journal record contains too many message parts.");
        writer.Write(received.Parts.Count);
        foreach (var part in received.Parts)
            WriteBytes(writer, part.AsReadOnlySpan());
        writer.Flush();
        if (stream.Length > MaxRecordBytes)
            throw new InvalidOperationException(
                "An accepted Spot journal record cannot exceed 64 MiB.");
        return stream.ToArray();
    }

    internal static ZLinkSpotAcceptedJournalRecord Decode(ReadOnlySpan<byte> encoded)
    {
        if (encoded.Length is <= 0 or > MaxRecordBytes)
            throw new InvalidDataException(
                "The accepted Spot journal record size is invalid.");
        using var stream = new MemoryStream(encoded.ToArray(), writable: false);
        using var reader = new BinaryReader(stream, Encoding.UTF8, leaveOpen: true);
        if (reader.ReadUInt32() != Magic || reader.ReadUInt16() != Version)
            throw new InvalidDataException(
                "The accepted Spot journal record header is invalid.");
        var sourceNodeRid = ReadRoutingId(reader);
        var spotId = ReadSpotId(reader);
        var requestSequence = reader.ReadBoolean()
            ? reader.ReadUInt64()
            : (ulong?)null;
        var metadataFrame = ReadBytes(reader);
        if (!ZLinkMeshMetadataCodec.TryDecode(metadataFrame, out var metadata))
            throw new InvalidDataException(
                "The accepted Spot journal metadata is invalid.");
        var partCount = reader.ReadInt32();
        if (partCount < 0 || partCount > MaxParts)
            throw new InvalidDataException(
                "The accepted Spot journal part count is invalid.");
        var parts = new ReadOnlyMemory<byte>[partCount];
        for (var index = 0; index < parts.Length; index++)
            parts[index] = ReadBytes(reader);
        if (stream.Position != stream.Length)
            throw new InvalidDataException(
                "The accepted Spot journal record contains trailing bytes.");
        return new ZLinkSpotAcceptedJournalRecord(
            sourceNodeRid,
            spotId,
            requestSequence,
            metadata,
            parts);
    }

    private static void WriteRoutingId(BinaryWriter writer, RoutingId? value)
    {
        writer.Write(value.HasValue);
        if (value is { } routingId)
            WriteBytes(writer, routingId.ToBytes());
    }

    private static RoutingId? ReadRoutingId(BinaryReader reader)
    {
        return reader.ReadBoolean()
            ? RoutingId.From(ReadBytes(reader))
            : null;
    }

    private static void WriteSpotId(BinaryWriter writer, string? value)
    {
        writer.Write(value is not null);
        if (value is null) return;
        WriteBytes(writer, Encoding.UTF8.GetBytes(
            ZLinkSpotId.Require(value, nameof(value))));
    }

    private static string? ReadSpotId(BinaryReader reader)
    {
        if (!reader.ReadBoolean()) return null;
        var encoded = ReadBytes(reader);
        try
        {
            var value = new UTF8Encoding(false, true).GetString(encoded);
            return ZLinkSpotId.IsValid(value)
                ? value
                : throw new InvalidDataException("The accepted Spot ID is invalid.");
        }
        catch (DecoderFallbackException error)
        {
            throw new InvalidDataException("The accepted Spot ID is not valid UTF-8.", error);
        }
    }

    private static void WriteBytes(BinaryWriter writer, ReadOnlySpan<byte> value)
    {
        writer.Write(value.Length);
        writer.Write(value);
    }

    private static byte[] ReadBytes(BinaryReader reader)
    {
        var length = reader.ReadInt32();
        if (length < 0 || length > MaxRecordBytes)
            throw new InvalidDataException(
                "An accepted Spot journal byte field exceeds its bound.");
        var value = reader.ReadBytes(length);
        if (value.Length != length)
            throw new EndOfStreamException();
        return value;
    }
}
