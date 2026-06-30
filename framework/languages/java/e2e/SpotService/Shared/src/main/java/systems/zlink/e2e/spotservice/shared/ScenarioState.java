package systems.zlink.e2e.spotservice.shared;

import java.util.ArrayList;
import java.util.List;

public final class ScenarioState {
    private final String nodeRid;
    private final List<Contracts.EvidenceEntry> entries = new ArrayList<>();

    public ScenarioState(String nodeRid) {
        this.nodeRid = nodeRid;
    }

    public String nodeRid() {
        return nodeRid;
    }

    public synchronized void record(String marker, String spotRid, String value) {
        entries.add(new Contracts.EvidenceEntry(marker, nodeRid, spotRid, value));
    }

    public synchronized Contracts.EvidenceSnapshot snapshot() {
        return new Contracts.EvidenceSnapshot(nodeRid, List.copyOf(entries));
    }
}
