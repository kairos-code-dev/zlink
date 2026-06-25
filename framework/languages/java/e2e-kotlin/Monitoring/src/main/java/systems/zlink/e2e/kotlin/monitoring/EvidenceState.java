package systems.zlink.e2e.kotlin.monitoring;

import java.util.ArrayList;
import java.util.List;

public final class EvidenceState {
    private final List<Contracts.EvidenceEntry> entries = new ArrayList<>();

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
