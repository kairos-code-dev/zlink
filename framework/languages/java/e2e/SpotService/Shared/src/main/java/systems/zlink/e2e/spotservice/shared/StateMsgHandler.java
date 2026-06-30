package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.spots.ZLinkSpotPacketHandler;

public final class StateMsgHandler
    implements ZLinkSpotPacketHandler<UserSpot, Contracts.StateMsg> {
    @Override
    public void handle(
        UserSpot spot,
        Contracts.StateMsg message) {
        spot.command(message.value());
    }
}
