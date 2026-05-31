/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.messaging.Message;
/** Callback invoked for each UInt32-length-framed packet on a stream socket. */
@FunctionalInterface
public interface StreamUInt32FramedPacketHandler {
    void onPacket(int routingId, Message header, Message body);
}
