package systems.zlink.samples.shoppingmall.server.orderworkflow.spots;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;

public final class OrderWorkflowSpot implements ZLinkSpot<ZLinkActor> {
    private final ZLinkSpotContext context;

    public OrderWorkflowSpot(ZLinkSpotContext context) {
        this.context = context;
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    @Override
    public void onJoinedActor(ZLinkActor actor, CancellationToken cancellationToken) {
    }

    @Override
    public void onLeaveActor(ZLinkActor actor, CancellationToken cancellationToken) {
    }
}
