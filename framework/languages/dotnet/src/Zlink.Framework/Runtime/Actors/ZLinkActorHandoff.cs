namespace Zlink.Framework.Runtime.Actors;

internal enum ZLinkActorFrameRoute
{
    Current,
    Forward,
    Stale
}

internal sealed class ZLinkActorHandoffRejectedException(
    string message,
    Exception? innerException = null) : InvalidOperationException(message, innerException);

internal sealed record ZLinkActorHandoffFrame(
    byte[] ReplyActorNodeRid,
    ulong ReplyActorGeneration,
    byte[] SourceNodeRid,
    byte[] SourceSessionRid,
    ulong RequestId,
    uint Flags,
    byte[] Header,
    byte[] Body,
    long ArrivalIndex,
    ZLinkBackendActorRouteContext RouteContext = default);

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
            arrivalIndex,
            frame.RouteContext);
    }

    private static RoutingId RidOrDefault(byte[] rid) =>
        rid is { Length: > 0 } ? RoutingId.From(rid) : default;

    public static ZLinkSpotActorFrameBatch Restore(
        ZLinkBackendActorRef actor,
        IReadOnlyList<ZLinkActorHandoffFrame> frames)
    {
        var restored = new List<ZLinkSpotActorFrame>(frames.Count);
        try
        {
            foreach (var frame in frames.OrderBy(static frame => frame.ArrivalIndex))
            {
                var replyActor = new ZLinkBackendActorRef(
                    RidOrDefault(frame.ReplyActorNodeRid),
                    actor.ActorId,
                    frame.ReplyActorGeneration);
                restored.Add(new ZLinkSpotActorFrame(
                    actor,
                    replyActor,
                    RidOrDefault(frame.SourceNodeRid),
                    // A caller-routed frame carries no session identity.
                    RidOrDefault(frame.SourceSessionRid),
                    frame.RequestId,
                    frame.Flags,
                    frame.RouteContext,
                    ZLinkStreamProtocolDefaults.DecodeHeader(frame.Header),
                    Message.From(frame.Body)));
            }

            return new ZLinkSpotActorFrameBatch(restored);
        }
        catch
        {
            foreach (var frame in restored) frame.Dispose();
            throw;
        }
    }
}
