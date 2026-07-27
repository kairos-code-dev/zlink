namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkActorHandoffIngress
{
    public static ZLinkSpotActorFrameBatch CaptureMovingFrames(
        ZLinkFrameworkRuntime runtime,
        IReadOnlyList<ZLinkBackendActorPart> parts)
    {
        var dispatchable = new List<ZLinkSpotActorFrame>(parts.Count / 2);
        var index = 0;
        while (index < parts.Count)
        {
            var headerPart = parts[index++];
            if (!ZLinkSpotActorFrameReader.TryRead(parts, ref index, headerPart, out var frame))
            {
                continue;
            }
            try
            {
                var state = runtime.GetOrCreateActorState(frame.Actor.ActorId);
                try
                {
                    if (ZLinkActorMessageFollowDispatcher.TryFollow(
                            runtime,
                            state,
                            frame.Actor,
                            frame.SourceNodeRid,
                            frame.SourceSessionRid,
                            frame.RequestId,
                            frame.Flags,
                            frame.RouteContext,
                            frame.Header,
                            frame.Body,
                            frame.SourceNodeGeneration,
                            frame.RequestSource,
                            frame.DirectReply))
                    {
                        frame.Dispose();
                        continue;
                    }
                }
                catch (ZLinkFrameworkException exception)
                    when (exception.Kind == ZLinkFrameworkErrorKind.ActorLocationStale)
                {
                    // The async dispatcher owns stale request replies.
                }

                // Capture here, in pump-event order: the dispatch batches run
                // detached and concurrently, so capturing inside the pipeline
                // would race sibling frames and break the backlog's arrival
                // sequence (spec 23 §10.2).
                if (state.Handoff.TryCapture(
                        frame,
                        () => ZLinkActorInboundPipeline.EnsureRelocationReplyRoute(
                            runtime,
                            frame))
                    == ZLinkActorHandoffCaptureResult.Captured)
                {
                    continue;
                }


                dispatchable.Add(frame);
            }
            catch
            {
                frame.Dispose();
                foreach (var dispatchableFrame in dispatchable) dispatchableFrame.Dispose();
                for (; index < parts.Count; index++) parts[index].Message.Dispose();
                throw;
            }
        }

        return new ZLinkSpotActorFrameBatch(dispatchable);
    }
}
