package systems.zlink.framework.locations;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;

public interface ZLinkLocationReadiness {
    CompletionStage<Boolean> isPeerReadyAsync(
        String meshName,
        ZLinkLocationRole role,
        RoutingId nodeRid);
}
