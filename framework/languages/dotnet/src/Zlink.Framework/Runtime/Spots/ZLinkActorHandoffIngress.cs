using Zlink.Framework.Runtime.Dispatch;

namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkActorHandoffIngress
{
    internal static bool TryCaptureOrFollowStaleManagedIngress(
        ZLinkFrameworkRuntime runtime,
        IReadOnlyList<ZLinkBackendActorPart> parts)
    {
        if (parts.Count == 0)
            return false;
        var headerPart = parts[0];
        var state = runtime.GetOrCreateActorState(headerPart.Actor.ActorId);
        if (!state.Handoff.CapturesSourceIngress
            && !ZLinkActorMessageFollowDispatcher.CanFollow(
                state,
                headerPart.Actor,
                headerPart.RouteContext))
            return false;

        var index = 1;
        if (!ZLinkSpotActorFrameReader.TryRead(
                parts,
                ref index,
                headerPart,
                out var frame))
            return true;
        var frameTransferred = false;
        try
        {
            var capture = state.Handoff.TryCapture(
                frame,
                () => ZLinkActorInboundPipeline.EnsureRelocationReplyRoute(
                    runtime,
                    frame));
            if (capture == ZLinkActorHandoffCaptureResult.Captured)
                return true;

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
                        frame.DirectReply,
                        frame.ApplicationMetadata))
                    return true;
            }
            catch (ZLinkFrameworkException exception)
                when (exception.Kind == ZLinkFrameworkErrorKind.Unavailable)
            {
                // The regular inbound pipeline returns the typed moving/stale
                // terminal after a capture-to-follow transition race.
            }

            var owned = new ZLinkSpotActorFrameBatch([frame]);
            frameTransferred = true;
            if (!runtime.TryRunDetached(
                    "actor-stale-managed-ingress",
                    cancellationToken => new ZLinkActorInboundPipeline(
                            runtime,
                            new ZLinkEntrySpotActorInboundEndpoint(runtime))
                        .DispatchAsync(owned, cancellationToken)))
                owned.Dispose();
            return true;
        }
        finally
        {
            if (!frameTransferred)
                frame.Dispose();
            for (; index < parts.Count; index++)
                parts[index].Message.Dispose();
        }
    }

    public static ZLinkSpotActorFrameBatch CaptureMovingFrames(
        ZLinkFrameworkRuntime runtime,
        IReadOnlyList<ZLinkBackendActorPart> parts,
        ZLinkInboundDispatchLease? inboundDispatchLease = null)
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
                            frame.DirectReply,
                            frame.ApplicationMetadata))
                    {
                        frame.Dispose();
                        continue;
                    }
                }
                catch (ZLinkFrameworkException exception)
                    when (exception.Kind == ZLinkFrameworkErrorKind.Unavailable)
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

        return new ZLinkSpotActorFrameBatch(
            dispatchable,
            inboundDispatchLease: inboundDispatchLease);
    }
}
