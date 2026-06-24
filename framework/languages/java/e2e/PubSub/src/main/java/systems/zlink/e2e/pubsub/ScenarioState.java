package systems.zlink.e2e.pubsub;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public final class ScenarioState {
    private final String subscriberRid;
    private final Set<String> topics;
    private final List<Contracts.EvidenceEntry> entries = new ArrayList<>();

    public ScenarioState(String subscriberRid, Set<String> topics) {
        this.subscriberRid = subscriberRid;
        this.topics = Set.copyOf(topics);
    }

    public String subscriberRid() {
        return subscriberRid;
    }

    public boolean accepts(String topic) {
        return topics.contains(topic);
    }

    public void delayIfConfigured(String scenario) {
        String delay = Env.get("ZLINK_JAVA_E2E_HANDLER_DELAY_MS");
        if (delay.isBlank() || !"ps-b1".equals(scenario)) {
            return;
        }
        try {
            Thread.sleep(Long.parseLong(delay));
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("subscriber delay interrupted", error);
        }
    }

    public synchronized void record(
        String marker,
        String topic,
        String scenario,
        int sequence,
        String value) {
        entries.add(new Contracts.EvidenceEntry(
            marker,
            subscriberRid,
            topic,
            scenario,
            sequence,
            value));
    }

    public synchronized Contracts.EvidenceSnapshot snapshot() {
        return new Contracts.EvidenceSnapshot(subscriberRid, List.copyOf(entries));
    }

    public static Set<String> topicsFromEnv() {
        Set<String> values = new HashSet<>();
        for (String part : Env.get("ZLINK_JAVA_E2E_TOPICS", "all").split(",")) {
            String trimmed = part.trim();
            if (!trimmed.isEmpty()) {
                values.add(trimmed);
            }
        }
        values.add("all");
        return values;
    }
}
