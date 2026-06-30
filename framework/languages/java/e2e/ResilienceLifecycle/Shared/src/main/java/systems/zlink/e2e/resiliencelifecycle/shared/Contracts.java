package systems.zlink.e2e.resiliencelifecycle.shared;

import java.util.List;

public final class Contracts {
    public static final String CHANNEL = "resilience.lifecycle.api";
    public static final String HANDLER_GROUP = "resilience-lifecycle";

    private Contracts() {
    }

    public record WorkReq(String value) {
    }

    public record WorkMsg(String value) {
    }

    public record UnhandledReq(String value) {
    }

    public record WorkRes(
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
