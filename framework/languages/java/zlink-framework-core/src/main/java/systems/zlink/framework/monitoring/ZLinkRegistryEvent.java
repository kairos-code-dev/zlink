package systems.zlink.framework.monitoring;

import java.time.Instant;
import java.util.List;
import java.util.Optional;
import systems.zlink.framework.registry.ZLinkRegistryServiceSummaryEntry;
import systems.zlink.framework.registry.ZLinkRegistryStatus;
import systems.zlink.framework.registry.ZLinkRegistryTopologyEntry;

public record ZLinkRegistryEvent(
    String sourceName,
    Instant timestamp,
    ZLinkRegistryEventKind event,
    Optional<ZLinkRegistryStatus> status,
    List<ZLinkRegistryTopologyEntry> topology,
    List<ZLinkRegistryServiceSummaryEntry> serviceSummary) implements ZLinkRuntimeEvent {
    public ZLinkRegistryEvent {
        status = status == null ? Optional.empty() : status;
        topology = topology == null ? List.of() : List.copyOf(topology);
        serviceSummary = serviceSummary == null ? List.of() : List.copyOf(serviceSummary);
    }
}
