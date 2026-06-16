package systems.zlink.samples.supportchat.server.support.adapters.zlink.spots.handlers;

import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.spots.ZLinkTimerTick;
import systems.zlink.samples.supportchat.server.support.adapters.zlink.spots.ConversationSpot;

public final class ConversationIdleTimerHandler implements ZLinkSpotTimerHandler<ConversationSpot> {
    @Override
    public void handle(ConversationSpot spot, ZLinkTimerTick tick) {
        spot.checkIdle();
    }
}
