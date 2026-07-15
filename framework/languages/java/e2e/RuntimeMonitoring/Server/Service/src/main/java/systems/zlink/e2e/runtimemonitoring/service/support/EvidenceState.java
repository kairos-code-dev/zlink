package systems.zlink.e2e.runtimemonitoring.service.support;

import java.util.ArrayList;
import java.util.List;
import systems.zlink.e2e.runtimemonitoring.shared.Contracts;
import systems.zlink.e2e.runtimemonitoring.shared.Env;

public final class EvidenceState {
    private final List<Contracts.EvidenceEntry> entries = new ArrayList<>();

    public String rid() {
        return Env.get("routingId", "svc-a");
    }

    public synchronized void record(
        String surface,
        String sourceName,
        String event,
        String detail) {
        entries.add(new Contracts.EvidenceEntry(surface, sourceName, event, detail));
    }

    public synchronized Contracts.EvidenceSnapshot snapshot() {
        return new Contracts.EvidenceSnapshot(List.copyOf(entries));
    }
}
