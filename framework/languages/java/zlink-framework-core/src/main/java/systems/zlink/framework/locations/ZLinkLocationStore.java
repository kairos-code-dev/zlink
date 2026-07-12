package systems.zlink.framework.locations;

import java.util.concurrent.CompletionStage;

public interface ZLinkLocationStore extends
    ZLinkPeerLocationStore,
    ZLinkSpotLocationStore,
    ZLinkActorLocationStore,
    ZLinkRouteLocationStore,
    ZLinkOwnerLeaseStore {
    CompletionStage<Long> removeAllByOwner(String ownerId);
}
