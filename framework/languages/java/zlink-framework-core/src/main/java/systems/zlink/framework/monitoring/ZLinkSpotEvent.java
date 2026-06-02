package systems.zlink.framework.monitoring;

import java.time.Instant;
import java.util.List;

public record ZLinkSpotEvent(
    String sourceName,
    Instant timestamp,
    ZLinkSpotEventKind event,
    List<String> peers,
    List<String> subjects,
    String timerDiagnostic) implements ZLinkRuntimeEvent {
}
