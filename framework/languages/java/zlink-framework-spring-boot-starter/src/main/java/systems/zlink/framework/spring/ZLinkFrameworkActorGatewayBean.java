package systems.zlink.framework.spring;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorGateway;
import systems.zlink.framework.actors.ZLinkActorJoinEntrySpotCall;
import systems.zlink.framework.actors.ZLinkActorJoinSpotCall;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.host.ZLinkFrameworkLifecycle;

final class ZLinkFrameworkActorGatewayBean implements ZLinkActorGateway {
    private final ZLinkFrameworkLifecycle lifecycle;

    ZLinkFrameworkActorGatewayBean(ZLinkFrameworkLifecycle lifecycle) {
        this.lifecycle = lifecycle;
    }

    @Override
    public ZLinkActorJoinSpotCall joinSpot(
        ZLinkActorRef actor,
        RoutingId spotRid,
        ZLinkMessage request) {
        return lifecycle.actorGateway().joinSpot(actor, spotRid, request);
    }

    @Override
    public ZLinkActorJoinEntrySpotCall joinEntrySpot(
        ZLinkActorRef actor,
        RoutingId spotNodeRid,
        ZLinkMessage request) {
        return lifecycle.actorGateway().joinEntrySpot(actor, spotNodeRid, request);
    }
}
