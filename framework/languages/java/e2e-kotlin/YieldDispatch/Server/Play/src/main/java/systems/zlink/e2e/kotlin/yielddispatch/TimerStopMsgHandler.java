package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.spots.ZLinkSpotPacketHandler;

public final class TimerStopMsgHandler
    implements ZLinkSpotPacketHandler<ProbeSpot, Contracts.TimerStopMsg> {
    @Override
    public void handle(
        ProbeSpot spot,
        Contracts.TimerStopMsg message) {
        spot.stopTimers(message.requestId());
    }
}
