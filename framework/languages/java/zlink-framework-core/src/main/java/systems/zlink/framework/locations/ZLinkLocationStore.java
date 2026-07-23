package systems.zlink.framework.locations;

import java.util.concurrent.CompletionStage;

public interface ZLinkLocationStore extends
    ZLinkMeshNodeLocationStore,
    ZLinkPeerLocationStore,
    ZLinkSpotLocationStore,
    ZLinkActorLocationStore,
    ZLinkRouteLocationStore,
    ZLinkOwnerLeaseStore,
    ZLinkAuthorityStore {
    CompletionStage<Long> removeAllByOwner(ZLinkLocationOwnerToken owner);
}
