namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkActorHandoffIngress
{
    public static IReadOnlyList<ZLinkBackendActorPart> CaptureMovingFrames(
        ZLinkFrameworkRuntime runtime,
        IReadOnlyList<ZLinkBackendActorPart> parts)
    {
        var dispatchable = new List<ZLinkBackendActorPart>(parts.Count);
        var index = 0;
        while (index < parts.Count)
        {
            var headerPart = parts[index++];
            if (!ZLinkSpotActorFrameReader.TryRead(parts, ref index, headerPart, out var frame))
                continue;

            var state = runtime.GetOrCreateActorState(frame.Actor.ActorId);
            if (state.NotifyHandoffFrameArrived())
                ZLinkFrameworkDebugLog.SpotDiscovery(
                    $"backlog_enqueued actor={frame.Actor.ActorId} trailing=true");
            if (state.TryCaptureHandoffFrame(frame))
            {
                frame.Body.Dispose();
                continue;
            }

            dispatchable.Add(new ZLinkBackendActorPart(
                frame.Actor,
                frame.SourceNodeRid,
                frame.SourceSessionRid,
                frame.RequestId,
                frame.Flags,
                Message.From(ZLinkStreamProtocolDefaults.EncodeHeader(frame.Header).Span),
                true));
            dispatchable.Add(new ZLinkBackendActorPart(
                frame.Actor,
                frame.SourceNodeRid,
                frame.SourceSessionRid,
                frame.RequestId,
                frame.Flags,
                frame.Body,
                false));
        }

        return dispatchable;
    }
}
