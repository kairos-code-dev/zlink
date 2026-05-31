/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.sockets.SocketType;

/**
 * Filters a spot node socket query; null fields match anything.
 * @param owner restrict to sockets owned by this component, or null for any
 * @param socketType restrict to this socket type, or null for any
 * @param socketName restrict to this socket name, or null for any
 */
public record SpotNodeSocketFilter(
    SpotNodeSocketOwner owner,
    SocketType socketType,
    String socketName) {
}
