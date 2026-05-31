/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.messaging.Received;
/** Callback invoked for each received message. */
@FunctionalInterface
public interface SocketMessageHandler {
    void onMessage(Received received);
}
