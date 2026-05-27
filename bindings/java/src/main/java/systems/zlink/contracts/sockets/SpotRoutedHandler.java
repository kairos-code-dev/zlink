/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.core.RoutingId;
@FunctionalInterface
public interface SpotRoutedHandler {
    void onMessage(RoutingId sourceRid, RoutingId spotRid, long requestSeq,
                   Received received);
}
