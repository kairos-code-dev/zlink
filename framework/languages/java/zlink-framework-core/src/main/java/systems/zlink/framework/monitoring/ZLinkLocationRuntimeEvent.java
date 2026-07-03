package systems.zlink.framework.monitoring;

import java.time.Instant;
import java.util.List;
import systems.zlink.framework.locations.ZLinkLocationRuntimeStatus;
import systems.zlink.framework.locations.ZLinkLocationServiceSummary;
import systems.zlink.framework.locations.ZLinkLocationTopologyEntry;

public record ZLinkLocationRuntimeEvent(
    String sourceName,
    Instant timestamp,
    ZLinkLocationRuntimeEventKind event,
    ZLinkLocationRuntimeStatus status,
    List<ZLinkLocationTopologyEntry> topology,
    List<ZLinkLocationServiceSummary> serviceSummary) implements ZLinkRuntimeEvent {
}
