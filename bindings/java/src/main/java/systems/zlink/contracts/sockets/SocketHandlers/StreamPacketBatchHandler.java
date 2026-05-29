/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.messaging.Message;
@FunctionalInterface
public interface StreamPacketBatchHandler {
    int onPackets(long routingId, Message[] packets);
}
