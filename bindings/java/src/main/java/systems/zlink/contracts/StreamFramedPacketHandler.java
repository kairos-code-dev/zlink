/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


@FunctionalInterface
interface StreamFramedPacketHandler {
    void onPacket(RoutingId routingId, Message header, Message body);
}
