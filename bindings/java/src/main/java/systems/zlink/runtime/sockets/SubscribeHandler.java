/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.core.RoutingId;
@FunctionalInterface
interface SubscribeHandler {
    void onMessage(RoutingId routingId, String topicId, Received received);
}
