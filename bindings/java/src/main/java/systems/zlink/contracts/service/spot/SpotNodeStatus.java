/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.core.RoutingId;

public record SpotNodeStatus(String channelName, String localEndpoint,
                             RoutingId nodeRoutingId, SpotNodeState state,
                             int configuredPeerCount, int activePeerCount,
                             int connectedPeerCount, int subjectCount,
                             int readySubjectCount,
                             int disconnectedSubTargetCount,
                             int disconnectedRoutedTargetCount, int lastError,
                             long lastChangedMs) {
}
