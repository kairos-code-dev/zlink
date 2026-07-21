package systems.zlink.framework.channels;

public record ZLinkLogicalMulticastDetail(
    long snapshotRemoteNodeCount,
    long admittedRemoteNodeCount,
    long droppedRemoteNodeCount,
    long unreachableRemoteNodeCount,
    long snapshotLocalSpotCount,
    long admittedLocalSpotCount,
    long droppedLocalSpotCount) {
}
