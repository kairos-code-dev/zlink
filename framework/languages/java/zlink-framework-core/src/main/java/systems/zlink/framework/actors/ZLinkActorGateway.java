package systems.zlink.framework.actors;

import systems.zlink.contracts.core.RoutingId;

public interface ZLinkActorGateway {
    default ZLinkActorJoinSpotCall joinSpot(ZLinkActorRef actor, RoutingId spotRid) {
        return joinSpot(actor, spotRid, systems.zlink.framework.messaging.ZLinkMessage.empty());
    }

    default ZLinkActorJoinSpotCall joinSpot(
        ZLinkActorRef actor,
        RoutingId spotRid,
        Object request) {
        return joinSpot(actor, spotRid, systems.zlink.framework.messaging.ZLinkMessage.of(request));
    }

    ZLinkActorJoinSpotCall joinSpot(
        ZLinkActorRef actor,
        RoutingId spotRid,
        systems.zlink.framework.messaging.ZLinkMessage request);

    default ZLinkActorJoinEntrySpotCall joinEntrySpot(
        ZLinkActorRef actor,
        RoutingId spotNodeRid) {
        return joinEntrySpot(
            actor,
            spotNodeRid,
            systems.zlink.framework.messaging.ZLinkMessage.empty());
    }

    default ZLinkActorJoinEntrySpotCall joinEntrySpot(
        ZLinkActorRef actor,
        RoutingId spotNodeRid,
        Object request) {
        return joinEntrySpot(
            actor,
            spotNodeRid,
            systems.zlink.framework.messaging.ZLinkMessage.of(request));
    }

    ZLinkActorJoinEntrySpotCall joinEntrySpot(
        ZLinkActorRef actor,
        RoutingId spotNodeRid,
        systems.zlink.framework.messaging.ZLinkMessage request);
}
