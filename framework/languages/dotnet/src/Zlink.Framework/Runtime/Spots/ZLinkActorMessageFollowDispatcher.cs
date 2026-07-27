namespace Zlink.Framework.Runtime.Spots;

internal static class ZLinkActorMessageFollowDispatcher
{
    public static bool TryFollow(
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
        ZLinkServiceWireCodec.RequestSourceFence? requestSource = null,
        Func<IReadOnlyList<Message>, SendFlags, SubmitResult>? directReply = null)
    {
        var route = actorState.Handoff.RouteFrame(
            actorState.NativeActorRef,
            frameActor,
            out var messageFollowRoute);
        if (route == ZLinkActorFrameRoute.MessageFollow)
        {
            ValidateMessageFollowRoute(messageFollowRoute!.Value, routeContext);
            runtime.ActorMessageFollower.Enqueue(
                messageFollowRoute.Value,
                sourceNodeRid,
                sourceSessionRid,
                requestId,
                flags,
                routeContext,
                header,
                body,
                sourceNodeGeneration,
                requestSource,
                directReply);
        }
        if (route is ZLinkActorFrameRoute.Stale
            or ZLinkActorFrameRoute.MessageFollowExpired)
        {
            runtime.LogActorHandoff(
                route == ZLinkActorFrameRoute.MessageFollowExpired
                    ? $"message_follow_expired actor={frameActor.ActorId} generation={frameActor.Generation}"
                    : $"message_follow_rejected actor={frameActor.ActorId} generation={frameActor.Generation}");
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorLocationStale,
                $"Actor ref '{frameActor.ActorId}' generation '{frameActor.Generation}' is stale.");
        }

        if (route == ZLinkActorFrameRoute.MessageFollow)
            runtime.LogActorHandoff(
                $"message_follow_relay actor={frameActor.ActorId} generation={frameActor.Generation}");

        return route == ZLinkActorFrameRoute.MessageFollow;
    }

    private static void ValidateMessageFollowRoute(
        ZLinkActorMessageFollowRoute messageFollowRoute,
        ZLinkBackendActorRouteContext route)
    {
        if (!messageFollowRoute.Lease.IsCommitted)
            throw Stale(messageFollowRoute, "the source-to-target route is not committed");
        if (!route.IsDirectRoute)
            return;
        if (route.MessageFollowHopCount >= 8)
            throw Stale(messageFollowRoute, "the Message Follow chain reached the 8-hop limit");
        if (route.TargetNodeGeneration != messageFollowRoute.SourceNodeGeneration
            || route.AuthorityOwnerGeneration
               != messageFollowRoute.SourceAuthorityOwnerGeneration
            || route.OwnerLeaseGeneration
               != messageFollowRoute.SourceOwnerLeaseGeneration)
            throw Stale(messageFollowRoute, "the incoming route fence does not match the committed source");
    }

    private static ZLinkFrameworkException Stale(
        ZLinkActorMessageFollowRoute messageFollowRoute,
        string reason) =>
        new(
            ZLinkFrameworkErrorKind.ActorLocationStale,
            $"Actor ref '{messageFollowRoute.SourceActor.ActorId}' cannot use Message Follow because {reason}.");

}
