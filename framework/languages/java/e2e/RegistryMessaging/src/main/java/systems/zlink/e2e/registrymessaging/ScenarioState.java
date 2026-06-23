package systems.zlink.e2e.registrymessaging;

import java.util.ArrayList;
import java.util.List;
import systems.zlink.e2e.registrymessaging.shared.Contracts;

public final class ScenarioState {
    private final List<Contracts.EvidenceEntry> entries = new ArrayList<>();
    private final String providerRid;
    private final String instanceId;

    public ScenarioState(String providerRid, String instanceId) {
        this.providerRid = providerRid;
        this.instanceId = instanceId;
    }

    public String providerRid() {
        return providerRid;
    }

    public String instanceId() {
        return instanceId;
    }

    public synchronized void record(String marker, String value) {
        entries.add(new Contracts.EvidenceEntry(marker, providerRid, value));
    }

    public synchronized Contracts.EvidenceSnapshot snapshot() {
        return new Contracts.EvidenceSnapshot(providerRid, List.copyOf(entries));
    }
}
