/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.messaging.Message;
@FunctionalInterface
public interface StreamUInt32FramedPacketHandler {
    void onPacket(int routingId, Message header, Message body);
}
