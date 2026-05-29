/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.sockets.SocketType;

public record SpotNodeSocketFilter(
    SpotNodeSocketOwner owner,
    SocketType socketType,
    String socketName) {
}
