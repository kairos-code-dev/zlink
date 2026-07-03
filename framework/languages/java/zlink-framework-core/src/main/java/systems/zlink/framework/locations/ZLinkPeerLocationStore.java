package systems.zlink.framework.locations;

import java.util.List;
import java.util.concurrent.CompletionStage;

public interface ZLinkPeerLocationStore {
    CompletionStage<ZLinkLocationWriteResult> updatePeerAsync(
        ZLinkPeerLocation peer,
        ZLinkLocationWriteIntent intent);

    CompletionStage<ZLinkLocationWriteResult> removePeerAsync(
        ZLinkPeerLocationKey key,
        ZLinkLocationOwnerToken owner);

    CompletionStage<Long> removePeersByOwnerAsync(String ownerId);

    CompletionStage<List<ZLinkPeerLocation>> listPeersAsync(ZLinkPeerLocationFilter filter);
}
