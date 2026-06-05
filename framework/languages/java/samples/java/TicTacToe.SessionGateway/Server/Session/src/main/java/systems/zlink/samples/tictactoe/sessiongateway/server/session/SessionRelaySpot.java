package systems.zlink.samples.tictactoe.sessiongateway.server.session;

import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;

public final class SessionRelaySpot implements ZLinkSpot {
    private final ZLinkSpotContext context;

    public SessionRelaySpot(ZLinkSpotContext context) {
        this.context = context;
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }
}
