package systems.zlink.framework.runtime.backend;

import java.util.List;
import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;

public record ZLinkBackendReceived(
    Optional<RoutingId> routingId,
    Optional<RoutingId> spotRid,
    Optional<Long> requestSeq,
    List<Message> parts) {
}
