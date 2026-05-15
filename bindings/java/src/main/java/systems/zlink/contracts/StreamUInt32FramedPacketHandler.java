/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


@FunctionalInterface
interface StreamUInt32FramedPacketHandler {
    void onPacket(int routingId, Message header, Message body);
}
