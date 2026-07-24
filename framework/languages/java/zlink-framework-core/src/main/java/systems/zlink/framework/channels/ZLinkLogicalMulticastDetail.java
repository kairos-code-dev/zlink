package systems.zlink.framework.channels;

/**
 * Reports the destinations observed and accepted while a logical multicast
 * was submitted. Remote admission means submission to the local transport
 * queue; local admission means submission to the target Spot queue. These
 * counts do not wait for a remote queue or any message handler to complete.
 */
public record ZLinkLogicalMulticastDetail(
    long snapshotRemoteNodeCount,
    long admittedRemoteNodeCount,
    long droppedRemoteNodeCount,
    long unreachableRemoteNodeCount,
    long snapshotLocalSpotCount,
    long admittedLocalSpotCount,
    long droppedLocalSpotCount) {
}
