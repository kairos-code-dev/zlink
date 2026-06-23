package systems.zlink.samples.deliverydispatch.server.tracking.spots;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.samples.deliverydispatch.server.tracking.actors.CustomerActor;

public final class CustomerEntrySpot implements ZLinkEntrySpot<CustomerActor> {
    private final ZLinkEntrySpotContext context;

    public CustomerEntrySpot(ZLinkEntrySpotContext context) {
        this.context = context;
    }

    @Override
    public ZLinkEntrySpotContext context() {
        return context;
    }

    @Override
    public void configure() {
    }

    @Override
    public void onCreateActor(CustomerActor actor, CancellationToken cancellationToken) {
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        CustomerActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken) {
        return ZLinkSpotActorJoinResponse.accept();
    }

    @Override
    public void onJoinedActor(CustomerActor actor, CancellationToken cancellationToken) {
    }

    @Override
    public void onLeaveActor(CustomerActor actor, CancellationToken cancellationToken) {
    }

    @Override
    public void onDisconnectActor(CustomerActor actor, CancellationToken cancellationToken) {
    }
}
