package systems.zlink.e2e.spotservice;

import systems.zlink.framework.spots.ZLinkSpotPacketHandler;

public final class StateCommandHandler
    implements ZLinkSpotPacketHandler<UserSpot, Contracts.StateCommand> {
    @Override
    public void handle(
        UserSpot spot,
        Contracts.StateCommand message) {
        spot.command(message.value());
    }
}
