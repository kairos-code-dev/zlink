namespace Zlink.Framework.Runtime.Spots;

internal readonly record struct ZLinkSpotActorFrame(
    ZLinkBackendActorRef Actor,
    RoutingId SourceNodeRid,
    RoutingId SourceSessionRid,
    ulong RequestId,
    uint Flags,
    ZlinkStreamHeader Header,
    Message Body);

internal static class ZLinkSpotActorFrameReader
{
    public static bool TryRead(
        IReadOnlyList<ZLinkBackendActorPart> parts,
        ref int index,
        ZLinkBackendActorPart headerPart,
        out ZLinkSpotActorFrame frame)
    {
        ZlinkStreamHeader header;
        try
        {
            header = ZLinkStreamProtocolDefaults.DecodeHeader(headerPart.Message.AsReadOnlyMemory());
        }
        catch
        {
            DisposeContinuationParts(parts, ref index, headerPart.More);
            throw;
        }
        finally
        {
            headerPart.Message.Dispose();
        }

        var body = TakeBodyPart(parts, ref index, headerPart.More);
        if (body is null)
        {
            frame = default;
            return false;
        }

        frame = new ZLinkSpotActorFrame(
            headerPart.Actor,
            headerPart.SourceNodeRid,
            headerPart.SourceSessionRid,
            headerPart.RequestId,
            headerPart.Flags,
            header,
            body);
        return true;
    }

    public static void DisposeFrame(
        IReadOnlyList<ZLinkBackendActorPart> parts,
        ref int index,
        ZLinkBackendActorPart headerPart)
    {
        headerPart.Message.Dispose();
        DisposeContinuationParts(parts, ref index, headerPart.More);
    }

    private static Message? TakeBodyPart(
        IReadOnlyList<ZLinkBackendActorPart> parts,
        ref int index,
        bool hasBody)
    {
        if (!hasBody) return Message.From(ReadOnlySpan<byte>.Empty);

        if (index >= parts.Count) return null;

        return parts[index++].Message;
    }

    private static void DisposeContinuationParts(
        IReadOnlyList<ZLinkBackendActorPart> parts,
        ref int index,
        bool hasMore)
    {
        while (hasMore && index < parts.Count)
        {
            var part = parts[index++];
            hasMore = part.More;
            part.Message.Dispose();
        }
    }
}
