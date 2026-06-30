package systems.zlink.e2e.spotservice.shared;

import systems.zlink.framework.spots.ZLinkSpotSubscriptionHandler;

public final class SpotEventHandler
    implements ZLinkSpotSubscriptionHandler<UserSpot, Contracts.MeshEvent> {
    @Override
    public void handle(
        UserSpot spot,
        Contracts.MeshEvent message) {
        spot.record("SpotMeshEvent", message.value());
    }
}
