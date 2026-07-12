package systems.zlink.framework.locations;

import java.util.List;
import java.util.concurrent.CompletionStage;

public interface ZLinkPeerLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updatePeer(
        ZLinkPeerLocation peer,
        ZLinkLocationWriteIntent intent);

    CompletionStage<ZLinkLocationWriteResult> removePeer(
        ZLinkPeerLocationKey key,
        ZLinkLocationOwnerToken owner);

    CompletionStage<List<ZLinkPeerLocation>> listPeerLocations(ZLinkPeerLocationFilter filter);
}
