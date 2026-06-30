package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.spots.ZLinkSpotPacketHandler;

public final class TimerStartCommandHandler
    implements ZLinkSpotPacketHandler<ProbeSpot, Contracts.TimerStartCommand> {
    @Override
    public void handle(
        ProbeSpot spot,
        Contracts.TimerStartCommand message) {
        spot.startTimer(message);
    }
}
