package systems.zlink.framework.runtime.backend;

import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;

public record ZLinkBackendSocketMonitorEvent(
    String event,
    Optional<RoutingId> routingId,
    String localAddress,
    String remoteAddress) {
}
