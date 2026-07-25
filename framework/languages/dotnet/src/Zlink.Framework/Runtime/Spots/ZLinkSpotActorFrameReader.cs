namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotActorFrame(
    ZLinkBackendActorRef actor,
    ZLinkBackendActorRef replyActor,
    RoutingId sourceNodeRid,
    RoutingId sourceSessionRid,
    ulong requestId,
    uint flags,
    ZLinkBackendActorRouteContext routeContext,
    ZlinkStreamHeader header,
    Message body) : IDisposable
{
    private Message? _body = body;

    public ZLinkBackendActorRef Actor { get; } = actor;

    public ZLinkBackendActorRef ReplyActor { get; } = replyActor;

    public RoutingId SourceNodeRid { get; } = sourceNodeRid;

    public RoutingId SourceSessionRid { get; } = sourceSessionRid;

    public ulong RequestId { get; } = requestId;

    public uint Flags { get; } = flags;

    public ZLinkBackendActorRouteContext RouteContext { get; } = routeContext;

    public ZlinkStreamHeader Header { get; } = header;

    public Message Body => _body
                           ?? throw new ObjectDisposedException(nameof(ZLinkSpotActorFrame));

    public void Dispose()
    {
        Interlocked.Exchange(ref _body, null)?.Dispose();
    }
}

internal sealed class ZLinkSpotActorFrameBatch(
    IReadOnlyList<ZLinkSpotActorFrame> frames,
    Action? completion = null) : IDisposable
{
    private int _disposed;

    public int Count => frames.Count;

    public ZLinkSpotActorFrame this[int index] => frames[index];

    public ZLinkSpotActorFrameBatch WithCompletion(Action onCompleted) =>
        new(frames, onCompleted);

    public void Dispose()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0) return;

        try
        {
            foreach (var frame in frames) frame.Dispose();
        }
        finally
        {
            completion?.Invoke();
        }
    }
}

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
            frame = null!;
            return false;
        }
        finally
        {
            headerPart.Message.Dispose();
        }

        var body = TakeBodyPart(parts, ref index, headerPart.More);
        if (body is null)
        {
            frame = null!;
            return false;
        }

        frame = new ZLinkSpotActorFrame(
            headerPart.Actor,
            headerPart.ReplyActor ?? headerPart.Actor,
            headerPart.SourceNodeRid,
            headerPart.SourceSessionRid,
            headerPart.RequestId,
            headerPart.Flags,
            headerPart.RouteContext,
            header,
            body);
        return true;
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
