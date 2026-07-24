/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** Snapshot of MeshNode monitor counters. */
public record MeshMonitorStatus(
    MeshNodeState state,
    long peerAdmitted,
    long peerRejected,
    long submittedMessages,
    long completedOperations,
    long backpressuredSubmits,
    long activeClaims,
    long pendingApplicationMessages,
    long pendingInfrastructureMessages,
    long pendingBytes) {
}
