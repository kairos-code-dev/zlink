/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

public record SpotNodePeerFilter(String peerEndpoint, SpotPeerSource source,
                                 SpotPeerState state) {
}
