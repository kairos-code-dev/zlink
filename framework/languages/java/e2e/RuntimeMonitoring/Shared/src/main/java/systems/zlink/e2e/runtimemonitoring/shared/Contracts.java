package systems.zlink.e2e.runtimemonitoring.shared;

import java.util.List;

public final class Contracts {
    public static final String CHANNEL = "monitoring.api";
    public static final String HANDSHAKE_CHANNEL = "monitoring.handshake";
    public static final String SPOT_MESH = "monitoring.spot.mesh";
    public static final String HANDLER_GROUP = "monitoring";
    public static final String LOCATION_SOURCE = "ops-locations";

    private Contracts() {
    }

    public record WorkReq(String value) {
    }

    public record WorkRes(String value, String providerRid) {
    }

    public record SpotSubjectProbe(String value) {
    }

    public record EvidenceEntry(String surface, String sourceName, String event, String detail) {
    }

    public record EvidenceSnapshot(List<EvidenceEntry> entries) {
    }
}
