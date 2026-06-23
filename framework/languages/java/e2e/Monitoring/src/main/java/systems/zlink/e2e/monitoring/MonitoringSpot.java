package systems.zlink.e2e.monitoring;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;

public final class MonitoringSpot implements ZLinkSpot<ZLinkActor> {
    private final ZLinkSpotContext context;

    public MonitoringSpot(ZLinkSpotContext context) {
        this.context = context;
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    @Override
    public ZLinkSpotCreateResponse onCreate(ZLinkMessage request) {
        return ZLinkSpotCreateResponse.accept();
    }
}
