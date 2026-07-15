namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkActorSessionForwarder
{
    public static bool TryForward(
        ZLinkFrameworkRuntime runtime,
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef frameActor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        ZlinkStreamHeader header,
        Message body)
    {
        var route = actorState.Handoff.RouteFrame(
            actorState.NativeActorRef,
            frameActor,
            out var targetActor,
            out var forwardingLease);
        if (route == ZLinkActorFrameRoute.Forward)
            runtime.ActorStragglerForwarder.Enqueue(
                frameActor,
                targetActor,
                sourceNodeRid,
                sourceSessionRid,
                requestId,
                flags,
                header,
                body,
                forwardingLease!);
        if (route == ZLinkActorFrameRoute.Stale)
        {
            runtime.LogActorHandoff(
                $"stale_fail_fast actor={frameActor.ActorId} generation={frameActor.Generation}");
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorLocationStale,
                $"Actor ref '{frameActor.ActorId}' generation '{frameActor.Generation}' is stale.");
        }

        if (route == ZLinkActorFrameRoute.Forward)
            runtime.LogActorHandoff(
                $"straggler_forward actor={frameActor.ActorId} generation={frameActor.Generation}");

        return route == ZLinkActorFrameRoute.Forward;
    }

}
