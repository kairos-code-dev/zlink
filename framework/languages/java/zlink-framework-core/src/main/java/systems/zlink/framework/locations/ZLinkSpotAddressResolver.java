package systems.zlink.framework.locations;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;

public interface ZLinkSpotAddressResolver {
    CompletionStage<ZLinkSpotAddress> resolveSpotAddressAsync(
        String meshName,
        RoutingId spotRid);
}
