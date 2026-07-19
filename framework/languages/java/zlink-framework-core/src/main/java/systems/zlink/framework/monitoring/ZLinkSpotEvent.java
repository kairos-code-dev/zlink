package systems.zlink.framework.monitoring;

import java.time.Instant;
import java.util.List;
import java.util.Optional;
import systems.zlink.contracts.service.spot.MeshNodeStatus;
import systems.zlink.contracts.service.spot.MeshPeerEntry;

public record ZLinkSpotEvent(
    String sourceName,
    Instant timestamp,
    ZLinkSpotEventKind event,
    Optional<MeshNodeStatus> status,
    List<MeshPeerEntry> peers,
    List<String> subjects,
    Optional<String> timerDiagnostic) implements ZLinkRuntimeEvent {
    public ZLinkSpotEvent {
        status = status == null ? Optional.empty() : status;
        peers = peers == null ? List.of() : List.copyOf(peers);
        subjects = subjects == null ? List.of() : List.copyOf(subjects);
        timerDiagnostic = timerDiagnostic == null ? Optional.empty() : timerDiagnostic;
    }
}
