package systems.zlink.samples.deliverydispatch.server.tracking.spots;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;

public final class CustomerEntrySpot implements ZLinkEntrySpot {
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
    public void onCreateActor(ZLinkActor actor, CancellationToken cancellationToken) {
    }

    @Override
    public void onJoinActor(ZLinkActor actor, CancellationToken cancellationToken) {
    }

    @Override
    public void onLeaveActor(ZLinkActor actor, CancellationToken cancellationToken) {
    }

    @Override
    public void onDisconnectActor(ZLinkActor actor, CancellationToken cancellationToken) {
    }
}
