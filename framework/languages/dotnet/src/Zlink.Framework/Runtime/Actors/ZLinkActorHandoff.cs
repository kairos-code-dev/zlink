namespace Zlink.Framework.Runtime.Actors;

internal enum ZLinkActorFrameRoute
{
    Current,
    Forward,
    Stale
}

internal sealed record ZLinkActorHandoffFrame(
    byte[] SourceNodeRid,
    byte[] SourceSessionRid,
    ulong RequestId,
    uint Flags,
    byte[] Header,
    byte[] Body,
    long ArrivalIndex);

internal static class ZLinkActorHandoffFrames
{
    public static ZLinkActorHandoffFrame Capture(
        ZLinkSpotActorFrame frame,
        long arrivalIndex)
    {
        return new ZLinkActorHandoffFrame(
            frame.SourceNodeRid.ToBytes().ToArray(),
            frame.SourceSessionRid.ToBytes().ToArray(),
            frame.RequestId,
            frame.Flags,
            ZLinkStreamProtocolDefaults.EncodeHeader(frame.Header).ToArray(),
            frame.Body.ToArray(),
            arrivalIndex);
    }

    public static IReadOnlyList<ZLinkBackendActorPart> Restore(
        ZLinkBackendActorRef actor,
        IReadOnlyList<ZLinkActorHandoffFrame> frames)
    {
        var parts = new List<ZLinkBackendActorPart>(frames.Count * 2);
        foreach (var frame in frames.OrderBy(static frame => frame.ArrivalIndex))
        {
            parts.Add(new ZLinkBackendActorPart(
                actor,
                RoutingId.From(frame.SourceNodeRid),
                RoutingId.From(frame.SourceSessionRid),
                frame.RequestId,
                frame.Flags,
                Message.From(frame.Header),
                true));
            parts.Add(new ZLinkBackendActorPart(
                actor,
                RoutingId.From(frame.SourceNodeRid),
                RoutingId.From(frame.SourceSessionRid),
                frame.RequestId,
                frame.Flags,
                Message.From(frame.Body),
                false));
        }

        return parts;
    }
}
