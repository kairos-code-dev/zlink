/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.core.RoutingId;
@FunctionalInterface
public interface StreamFramedPacketHandler {
    void onPacket(RoutingId routingId, Message header, Message body);
}
