package systems.zlink.e2e.spotservice;

import java.util.List;

public final class Contracts {
    public static final String ROUTE_CHANNEL = "spot.service.route";
    public static final String EGRESS_CHANNEL = "spot.service.egress";
    public static final String INGRESS_CHANNEL = "spot.service.ingress";
    public static final String SPOT_MESH = "spot.service.mesh";
    public static final String SPOT_NODE = "play";

    private Contracts() {
    }

    public record StateRequest(String op) {
    }

    public record StateReply(
        String spotRid,
        String nodeRid,
        String value) {
    }

    public record StateCommand(String value) {
    }

    public record SlowRequest(String value) {
    }

    public record EvidenceEntry(
        String marker,
        String nodeRid,
        String spotRid,
        String value) {
    }

    public record EvidenceSnapshot(
        String nodeRid,
        List<EvidenceEntry> entries) {
    }
}
