/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.core.RoutingId;

/**
 * A snapshot of a spot node's status and peer/subject counts.
 * @param channelName the logical channel name
 * @param localEndpoint the node's local transport endpoint
 * @param nodeRoutingId the node's routing id, when assigned
 * @param state the node's overall readiness state
 * @param configuredPeerCount the number of configured peers
 * @param activePeerCount the number of active peers
 * @param connectedPeerCount the number of connected peers
 * @param subjectCount the total number of subjects
 * @param readySubjectCount the number of ready subjects
 * @param disconnectedSubTargetCount the number of disconnected subscription targets
 * @param disconnectedRoutedTargetCount the number of disconnected routed targets
 * @param lastError the last native error code, or 0 for none
 * @param lastChangedMs when the status last changed, in milliseconds
 */
public record SpotNodeStatus(String channelName, String localEndpoint,
                             RoutingId nodeRoutingId, SpotNodeState state,
                             int configuredPeerCount, int activePeerCount,
                             int connectedPeerCount, int subjectCount,
                             int readySubjectCount,
                             int disconnectedSubTargetCount,
                             int disconnectedRoutedTargetCount, int lastError,
                             long lastChangedMs) {
}
