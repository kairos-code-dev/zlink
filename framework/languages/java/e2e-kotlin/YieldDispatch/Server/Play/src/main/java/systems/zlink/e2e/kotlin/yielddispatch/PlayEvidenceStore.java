package systems.zlink.e2e.kotlin.yielddispatch;

import java.util.ArrayList;
import java.util.List;

public final class PlayEvidenceStore {
    private final List<EvidenceEntry> entries = new ArrayList<>();

    public synchronized void record(
        String requestId,
        String marker,
        String value) {
        entries.add(new EvidenceEntry(requestId, marker, value));
    }

    public synchronized Contracts.EvidenceReply evidence(String requestId) {
        List<String> markers = entries.stream()
            .filter(entry -> entry.requestId().equals(requestId))
            .map(entry -> entry.marker() + "|" + entry.value())
            .toList();
        return new Contracts.EvidenceReply(requestId, markers);
    }

    private record EvidenceEntry(String requestId, String marker, String value) {
    }
}
