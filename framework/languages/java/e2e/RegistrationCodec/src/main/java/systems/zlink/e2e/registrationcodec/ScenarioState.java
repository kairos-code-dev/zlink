package systems.zlink.e2e.registrationcodec;

import java.util.ArrayList;
import java.util.List;

public final class ScenarioState {
    private final List<Contracts.EvidenceEntry> entries = new ArrayList<>();

    public synchronized void record(String marker, String packetName, String value) {
        entries.add(new Contracts.EvidenceEntry(marker, packetName, value));
    }

    public synchronized Contracts.EvidenceSnapshot snapshot() {
        return new Contracts.EvidenceSnapshot(List.copyOf(entries));
    }
}
