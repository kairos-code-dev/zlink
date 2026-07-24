package systems.zlink.framework.spots;

import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;

public interface ZLinkSpotManager {
    ZLinkSpotCreateCall create(String spotType);
    ZLinkSpotGetOrCreateCall getOrCreate(RoutingId spotRid, String spotType);
    CompletionStage<Optional<SpotRef>> find(RoutingId spotRid);
    CompletionStage<Boolean> close(SpotRef spot);
}
