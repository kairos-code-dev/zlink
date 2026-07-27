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
        ZLinkBackendActorRouteContext routeContext,
        ZlinkStreamHeader header,
        Message body,
        ulong sourceNodeGeneration = 0,
        ZLinkServiceWireCodec.RequestSourceFence? requestSource = null)
    {
        var route = actorState.Handoff.RouteFrame(
            actorState.NativeActorRef,
            frameActor,
            out var forwarding);
        if (route == ZLinkActorFrameRoute.Forward)
        {
            ValidateCommittedDirectRoute(forwarding!.Value, routeContext);
            runtime.ActorStragglerForwarder.Enqueue(
                forwarding.Value,
                sourceNodeRid,
                sourceSessionRid,
                requestId,
                flags,
                routeContext,
                header,
                body,
                sourceNodeGeneration,
                requestSource);
        }
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

    private static void ValidateCommittedDirectRoute(
        ZLinkActorForwardingMapping mapping,
        ZLinkBackendActorRouteContext route)
    {
        if (!mapping.Lease.IsCommitted)
            throw Stale(mapping, "the source-to-target mapping is not committed");
        if (!route.IsDirectRoute)
            return;
        if (route.ForwardingHopCount >= 8)
            throw Stale(mapping, "the forwarding chain reached the 8-hop limit");
        if (route.TargetNodeGeneration != mapping.SourceNodeGeneration
            || route.AuthorityOwnerGeneration
               != mapping.SourceAuthorityOwnerGeneration
            || route.OwnerLeaseGeneration
               != mapping.SourceOwnerLeaseGeneration)
            throw Stale(mapping, "the incoming route fence does not match the committed source");
    }

    private static ZLinkFrameworkException Stale(
        ZLinkActorForwardingMapping mapping,
        string reason) =>
        new(
            ZLinkFrameworkErrorKind.ActorLocationStale,
            $"Actor ref '{mapping.SourceActor.ActorId}' cannot be forwarded because {reason}.");

}
