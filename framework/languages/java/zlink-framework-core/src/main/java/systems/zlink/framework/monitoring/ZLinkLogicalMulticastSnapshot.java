package systems.zlink.framework.monitoring;

public record ZLinkLogicalMulticastSnapshot(
    long submitted,
    long backpressured,
    long dropped,
    long remoteSnapshotCount,
    long remoteAdmittedCount,
    long remoteDroppedCount,
    long localSnapshotCount,
    long localAdmittedCount,
    long localDroppedCount) {
}
