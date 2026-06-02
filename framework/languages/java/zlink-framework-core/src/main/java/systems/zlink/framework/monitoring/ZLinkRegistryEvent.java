package systems.zlink.framework.monitoring;

import java.time.Instant;
import java.util.List;

public record ZLinkRegistryEvent(
    String sourceName,
    Instant timestamp,
    ZLinkRegistryEventKind event,
    List<String> topology,
    List<String> serviceSummary) implements ZLinkRuntimeEvent {
}
