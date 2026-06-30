package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.spots.ZLinkSpotPacketHandler;

public final class TimerStopCommandHandler
    implements ZLinkSpotPacketHandler<ProbeSpot, Contracts.TimerStopCommand> {
    @Override
    public void handle(
        ProbeSpot spot,
        Contracts.TimerStopCommand message) {
        spot.stopTimers(message.requestId());
    }
}
