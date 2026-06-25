package systems.zlink.e2e.resiliencelifecycle;

import java.util.List;

public final class Contracts {
    public static final String CHANNEL = "resilience.lifecycle.api";
    public static final String HANDLER_GROUP = "resilience-lifecycle";

    private Contracts() {
    }

    public record WorkRequest(String value) {
    }

    public record WorkCommand(String value) {
    }

    public record UnhandledRequest(String value) {
    }

    public record WorkReply(
        String value,
        String providerRid) {
    }

    public record EvidenceEntry(
        String marker,
        String providerRid,
        String value) {
    }

    public record EvidenceSnapshot(
        String providerRid,
        int weight,
        List<EvidenceEntry> entries) {
    }
}
