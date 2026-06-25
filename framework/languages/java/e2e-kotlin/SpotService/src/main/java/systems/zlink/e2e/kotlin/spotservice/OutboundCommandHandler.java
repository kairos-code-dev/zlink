package systems.zlink.e2e.kotlin.spotservice;

import systems.zlink.framework.spots.ZLinkSpotPacketHandler;

public final class OutboundCommandHandler
    implements ZLinkSpotPacketHandler<UserSpot, Contracts.OutboundCommand> {
    @Override
    public void handle(
        UserSpot spot,
        Contracts.OutboundCommand message) {
        spot.record("SpotToSpotSend", message.value());
    }
}
