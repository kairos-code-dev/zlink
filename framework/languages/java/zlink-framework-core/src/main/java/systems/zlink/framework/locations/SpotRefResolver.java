package systems.zlink.framework.locations;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;

public interface SpotRefResolver {
    CompletionStage<SpotRef> resolveSpotRefAsync(
        String meshName,
        RoutingId spotRid);
}
