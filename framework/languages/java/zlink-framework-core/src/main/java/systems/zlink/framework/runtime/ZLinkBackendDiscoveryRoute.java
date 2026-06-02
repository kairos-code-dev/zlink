package systems.zlink.framework.runtime;

import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;

public record ZLinkBackendDiscoveryRoute(
    Optional<RoutingId> nodeRid,
    Optional<String> endpoint) {
}
