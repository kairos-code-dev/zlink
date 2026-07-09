package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.spots.ZLinkSpotPacketHandler;

public final class MultiNodeStateMsgHandler
    implements ZLinkSpotPacketHandler<MultiNodeSpot, Contracts.MultiNodeStateMsg> {
    @Override
    public void handle(
        MultiNodeSpot spot,
        Contracts.MultiNodeStateMsg message) {
        spot.command(message.marker());
    }
}
