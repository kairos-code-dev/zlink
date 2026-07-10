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
            if (state.TryCaptureHandoffFrame(frame))
            {
                frame.Body.Dispose();
                continue;
            }

            try
            {
                if (ZLinkActorSessionForwarder.ShouldForward(state, frame.Actor, out var targetActor))
                {
                    using (frame.Body)
                        ZLinkActorSessionForwarder.Forward(
                            runtime,
                            state,
                            targetActor,
                            frame.SourceNodeRid,
                            frame.SourceSessionRid,
                            frame.Header,
                            frame.Body);
                    continue;
                }
            }
            catch (ZLinkFrameworkException exception)
                when (exception.Kind == ZLinkFrameworkErrorKind.ActorLocationStale)
            {
                // The async dispatcher owns stale request replies.
            }

            dispatchable.Add(new ZLinkBackendActorPart(
                frame.Actor,
                frame.SourceNodeRid,
                frame.SourceSessionRid,
                frame.RequestId,
                frame.Flags,
                Message.From(ZLinkStreamProtocolDefaults.EncodeHeader(frame.Header).Span),
                true,
                frame.ReplyActor));
            dispatchable.Add(new ZLinkBackendActorPart(
                frame.Actor,
                frame.SourceNodeRid,
                frame.SourceSessionRid,
                frame.RequestId,
                frame.Flags,
                frame.Body,
                false,
                frame.ReplyActor));
        }

        return dispatchable;
    }
}
