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
                if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
                    Console.Error.WriteLine(
                        $"[ingress] frame read failed actor={headerPart.Actor.ActorId} req={headerPart.RequestId} flags={headerPart.Flags}");
                continue;
            }
            try
            {
                var state = runtime.GetOrCreateActorState(frame.Actor.ActorId);
                try
                {
                    if (ZLinkActorSessionForwarder.TryForward(
                            runtime,
                            state,
                            frame.Actor,
                            frame.SourceNodeRid,
                            frame.SourceSessionRid,
                            frame.RequestId,
                            frame.Flags,
                            frame.Header,
                            frame.Body))
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
                if (state.Handoff.TryCapture(frame))
                {
                    if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
                        Console.Error.WriteLine(
                            $"[ingress] captured actor={frame.Actor.ActorId} name={frame.Header.Name}");
                    continue;
                }

                if (Environment.GetEnvironmentVariable("ZLINK_DEBUG_PUMP") == "1")
                    Console.Error.WriteLine(
                        $"[ingress] pass actor={frame.Actor.ActorId} name={frame.Header.Name}");

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
