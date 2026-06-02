package systems.zlink.framework.monitoring;

import java.time.Instant;
import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;

public record ZLinkSocketEvent(
    String sourceName,
    Instant timestamp,
    ZLinkSocketEventKind event,
    Optional<RoutingId> routingId,
    String localAddr,
    String remoteAddr,
    Optional<ZLinkSocketDiagnostic> diagnostic) implements ZLinkRuntimeEvent {
}
