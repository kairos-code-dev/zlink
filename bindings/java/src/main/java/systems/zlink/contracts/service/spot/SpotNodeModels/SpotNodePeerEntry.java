/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/**
 * One peer of a spot node and its connection details.
 * @param channelName the logical channel name
 * @param localEndpoint the node's local transport endpoint
 * @param peerEndpoint the peer's transport endpoint
 * @param source how this peer became known
 * @param kind the peer's connection style
 * @param state the peer's connection state
 * @param weight the peer's load-balancing weight
 * @param connectedSinceMs when the peer connected, in milliseconds
 * @param lastChangedMs when the peer last changed state, in milliseconds
 */
public record SpotNodePeerEntry(String channelName, String localEndpoint,
                                String peerEndpoint, SpotPeerSource source,
                                SpotPeerKind kind, SpotPeerState state, int weight,
                                long connectedSinceMs, long lastChangedMs) {
}
