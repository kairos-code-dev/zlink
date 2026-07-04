package systems.zlink.framework.locations;

import java.util.List;
import java.util.concurrent.CompletionStage;

public interface ZLinkPeerLocationResolver {
    CompletionStage<List<ZLinkPeerLocation>> listLivePeersAsync(ZLinkPeerLocationFilter filter);
}
