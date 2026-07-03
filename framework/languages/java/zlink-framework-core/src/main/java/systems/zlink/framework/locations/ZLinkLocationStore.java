package systems.zlink.framework.locations;

public interface ZLinkLocationStore extends
    ZLinkPeerLocationStore,
    ZLinkSpotLocationStore,
    ZLinkActorLocationStore,
    ZLinkRouteLocationStore,
    ZLinkOwnerLeaseStore {
}
