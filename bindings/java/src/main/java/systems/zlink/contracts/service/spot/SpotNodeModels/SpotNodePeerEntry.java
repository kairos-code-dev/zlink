/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

public record SpotNodePeerEntry(String channelName, String localEndpoint,
                                String peerEndpoint, SpotPeerSource source,
                                SpotPeerKind kind, SpotPeerState state, int weight,
                                long connectedSinceMs, long lastChangedMs) {
}
