namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkActorSessionForwarder
{
    public static bool ShouldForward(
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef frameActor,
        out ZLinkBackendActorRef targetActor)
    {
        var route = actorState.ResolveFrameRoute(frameActor, out targetActor);
        if (route == ZLinkActorFrameRoute.Stale)
        {
            ZLinkFrameworkDebugLog.SpotDiscovery(
                $"stale_fail_fast actor={frameActor.ActorId} generation={frameActor.Generation}");
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorLocationStale,
                $"Actor ref '{frameActor.ActorId}' generation '{frameActor.Generation}' is stale.");
        }

        if (route == ZLinkActorFrameRoute.Forward)
            ZLinkFrameworkDebugLog.SpotDiscovery(
                $"straggler_forward actor={frameActor.ActorId} generation={frameActor.Generation}");

        return route == ZLinkActorFrameRoute.Forward;
    }

    public static void Forward(
        ZLinkFrameworkRuntime runtime,
        ZLinkActorRuntimeState actorState,
        ZLinkBackendActorRef targetActor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ZlinkStreamHeader header,
        Message body)
    {
        actorState.ForwardFrame(() =>
        {
            var headerBytes = ZLinkStreamProtocolDefaults.EncodeHeader(header).ToArray();
            using var headerPart = Message.From(headerBytes);
            if (!runtime.ForwardActorBoundSessionPart(
                    targetActor,
                    sourceNodeRid,
                    sourceSessionRid,
                    headerPart,
                    true,
                    SendFlags.None))
                throw new InvalidOperationException("Actor session header forward failed.");

            if (!runtime.ForwardActorBoundSessionPart(
                    targetActor,
                    sourceNodeRid,
                    sourceSessionRid,
                    body,
                    false,
                    SendFlags.None))
                throw new InvalidOperationException("Actor session body forward failed.");
        });
    }
}
