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

    CompletionStage<List<ZLinkPeerLocation>> listPeerLocationsAsync(ZLinkPeerLocationFilter filter);
}
