package systems.zlink.framework.monitoring;

import java.time.Instant;
import java.util.List;
import java.util.Optional;

public record ZLinkSpotEvent(
    String sourceName,
    Instant timestamp,
    ZLinkSpotEventKind event,
    Optional<ZLinkMeshNodeState> state,
    List<ZLinkMeshPeerSnapshot> peers,
    List<String> subjects,
    Optional<String> timerDiagnostic) implements ZLinkRuntimeEvent {
    public ZLinkSpotEvent {
        state = state == null ? Optional.empty() : state;
        peers = peers == null ? List.of() : List.copyOf(peers);
        subjects = subjects == null ? List.of() : List.copyOf(subjects);
        timerDiagnostic = timerDiagnostic == null ? Optional.empty() : timerDiagnostic;
    }
}
