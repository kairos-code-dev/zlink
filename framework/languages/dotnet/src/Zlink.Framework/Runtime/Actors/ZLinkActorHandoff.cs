namespace Zlink.Framework.Runtime.Actors;

internal enum ZLinkActorFrameRoute
{
    Current,
    Forward,
    Stale
}

internal sealed record ZLinkActorHandoffFrame(
    byte[] ReplyActorNodeRid,
    ulong ReplyActorGeneration,
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
            frame.ReplyActor.NodeRid.ToBytes().ToArray(),
            frame.ReplyActor.Generation,
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
            var replyActor = new ZLinkBackendActorRef(
                RoutingId.From(frame.ReplyActorNodeRid),
                actor.ActorId,
                frame.ReplyActorGeneration);
            parts.Add(new ZLinkBackendActorPart(
                actor,
                RoutingId.From(frame.SourceNodeRid),
                RoutingId.From(frame.SourceSessionRid),
                frame.RequestId,
                frame.Flags,
                Message.From(frame.Header),
                true,
                replyActor));
            parts.Add(new ZLinkBackendActorPart(
                actor,
                RoutingId.From(frame.SourceNodeRid),
                RoutingId.From(frame.SourceSessionRid),
                frame.RequestId,
                frame.Flags,
                Message.From(frame.Body),
                false,
                replyActor));
        }

        return parts;
    }
}
