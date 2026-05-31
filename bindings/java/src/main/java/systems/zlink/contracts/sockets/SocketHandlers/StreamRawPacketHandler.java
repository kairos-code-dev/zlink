/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.core.RoutingId;
/** Callback invoked for each raw packet on a stream socket. */
@FunctionalInterface
public interface StreamRawPacketHandler {
    int onPacket(RoutingId routingId, Message payload);
}
