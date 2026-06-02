package systems.zlink.framework.runtime.backend;

import java.util.List;
import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;

public record ZLinkBackendTopicMessage(
    Optional<RoutingId> routingId,
    String topic,
    List<Message> parts) {
}
