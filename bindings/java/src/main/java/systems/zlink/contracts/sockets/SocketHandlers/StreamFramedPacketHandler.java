/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.core.RoutingId;
/** Callback invoked for each length-framed packet on a stream socket. */
@FunctionalInterface
public interface StreamFramedPacketHandler {
    void onPacket(RoutingId routingId, Message header, Message body);
}
