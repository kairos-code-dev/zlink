package systems.zlink.framework.spots;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;

public interface ZLinkSpotRemoteAddressResolver {
    CompletionStage<String> resolveAsync(RoutingId spotNodeRid);
}
