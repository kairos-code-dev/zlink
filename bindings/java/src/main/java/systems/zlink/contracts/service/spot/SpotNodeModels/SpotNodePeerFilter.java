/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/**
 * Filters a spot node peer query; null fields match anything.
 * @param peerEndpoint restrict to this peer endpoint, or null for any
 * @param source restrict to peers from this source, or null for any
 * @param state restrict to peers in this state, or null for any
 */
public record SpotNodePeerFilter(String peerEndpoint, SpotPeerSource source,
                                 SpotPeerState state) {
}
