package systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.conversationspot.handlers;

import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.spots.ZLinkTimerTick;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.conversationspot.ConversationSpot;

public final class ConversationIdleTimerHandler implements ZLinkSpotTimerHandler<ConversationSpot> {
    @Override
    public void handle(ConversationSpot spot, ZLinkTimerTick tick) {
        spot.checkIdle();
    }
}
