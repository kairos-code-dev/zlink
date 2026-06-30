package systems.zlink.e2e.pubsub.subscriber.Infrastructure;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import systems.zlink.e2e.pubsub.shared.Contracts;
import systems.zlink.e2e.pubsub.subscriber.Configuration.SubscriberOptions;

public final class EvidenceStore {
    private final SubscriberOptions options;
    private final List<Contracts.EvidenceEntry> entries = new ArrayList<>();

    public EvidenceStore(SubscriberOptions options) {
        this.options = options;
    }

    public String subscriberRid() {
        return options.rid();
    }

    public boolean accepts(String topic) {
        return options.topics().contains(topic);
    }

    public void delayIfConfigured(String scenario) {
        if (options.delay().delayMillis() <= 0 || !"ps-b1".equals(scenario)) {
            return;
        }
        try {
            Thread.sleep(options.delay().delayMillis());
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
            options.rid(),
            topic,
            scenario,
            sequence,
            value));
    }

    public synchronized Contracts.EvidenceSnapshot snapshot() {
        return new Contracts.EvidenceSnapshot(options.rid(), List.copyOf(entries));
    }

    public Set<String> topics() {
        return options.topics();
    }
}
