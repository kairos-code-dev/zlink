/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

@FunctionalInterface
interface StreamRawPacketHandler {
    int onPacket(RoutingId routingId, Message payload);
}
