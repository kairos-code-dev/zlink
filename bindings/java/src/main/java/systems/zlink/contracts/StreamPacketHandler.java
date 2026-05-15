/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


@FunctionalInterface
public interface StreamPacketHandler {
    void onPacket(RoutingId routingId, Message header, Message body);
}
