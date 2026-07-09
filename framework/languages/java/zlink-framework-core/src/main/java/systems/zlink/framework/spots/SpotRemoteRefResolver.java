package systems.zlink.framework.spots;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;

public interface SpotRemoteRefResolver {
    CompletionStage<SpotRemoteRef> resolveSpotRemoteRefAsync(
        RoutingId spotRid);
}
