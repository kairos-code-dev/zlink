package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.spots.ZLinkSpotPacketHandler;

public final class OutboundMsgHandler
    implements ZLinkSpotPacketHandler<UserSpot, Contracts.OutboundMsg> {
    @Override
    public void handle(
        UserSpot spot,
        Contracts.OutboundMsg message) {
        spot.record("SpotToSpotSend", message.value());
    }
}
