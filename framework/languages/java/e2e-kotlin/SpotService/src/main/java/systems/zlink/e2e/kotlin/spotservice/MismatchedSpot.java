package systems.zlink.e2e.kotlin.spotservice;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;

public final class MismatchedSpot implements ZLinkSpot<ZLinkActor> {
    private final ZLinkSpotContext context;

    public MismatchedSpot(ZLinkSpotContext context) {
        this.context = context;
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    @Override
    public void configure() {
    }
}
