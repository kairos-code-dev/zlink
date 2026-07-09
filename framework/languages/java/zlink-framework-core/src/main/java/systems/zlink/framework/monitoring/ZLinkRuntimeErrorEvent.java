package systems.zlink.framework.monitoring;

import java.time.Instant;
import java.util.Optional;

public record ZLinkRuntimeErrorEvent(
    String sourceName,
    Instant timestamp,
    ZLinkRuntimeErrorEventKind event,
    String callbackName,
    String exceptionType,
    Optional<String> message) implements ZLinkRuntimeEvent {
    public ZLinkRuntimeErrorEvent {
        message = message == null ? Optional.empty() : message;
    }
}
