package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.spots.ZLinkSpotPacketHandler;

public final class TimerStartMsgHandler
    implements ZLinkSpotPacketHandler<ProbeSpot, Contracts.TimerStartMsg> {
    @Override
    public void handle(
        ProbeSpot spot,
        Contracts.TimerStartMsg message) {
        spot.startTimer(message);
    }
}
