package systems.zlink.e2e.runtimemonitoring.shared;

import java.util.List;

public final class Contracts {
    public static final String CHANNEL = "monitoring.api";
    public static final String HANDSHAKE_CHANNEL = "monitoring.handshake";
    public static final String SPOT_MESH = "monitoring.spot.mesh";
    public static final String SPOT_CHANNEL = "monitoring.spot.runtime";
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

    public record MulticastProbe(String value) {
    }

    public record PublishCommand(
        String topic,
        String value,
        int count,
        boolean stopOnLocalDrop) {
    }

    public record PublishOutcome(
        String status,
        long snapshotRemote,
        long admittedRemote,
        long droppedRemote,
        long snapshotLocal,
        long admittedLocal,
        long droppedLocal,
        int attempts) {
    }

    public record EvidenceEntry(String surface, String sourceName, String event, String detail) {
    }

    public record EvidenceSnapshot(List<EvidenceEntry> entries) {
    }

    public record ObserverIsolationStatus(
        boolean started,
        long normalEventCount,
        long normalLatestSequence,
        long slowLatestSequence,
        boolean slowFailed) {
    }

    public record RuntimePeer(
        String rid,
        long lifecycleGeneration,
        long descriptorRevision,
        String endpoint,
        String admissionState,
        boolean ready,
        List<String> channelNames,
        String lastFailure) {
    }

    public record RuntimeChannel(
        String channelName,
        int localWeight,
        long readyMemberCount,
        boolean selectable) {
    }

    public record RuntimeSnapshot(
        String meshName,
        String rid,
        long lifecycleGeneration,
        long descriptorRevision,
        String endpoint,
        String state,
        long sequence,
        String observedAt,
        List<String> descriptorSources,
        List<RuntimePeer> peers,
        List<RuntimeChannel> channels,
        long multicastSubmitted,
        long multicastBackpressured,
        long multicastDropped,
        boolean applicationClaimActive,
        long applicationClaimPending,
        boolean infrastructureClaimActive,
        long infrastructureClaimPending,
        String locationState,
        String locationLastSuccess,
        String locationLastFailure,
        String drainState,
        boolean workSealed,
        long pendingApplication,
        long pendingTransfers,
        long pendingStreamBarriers) {
    }
}
