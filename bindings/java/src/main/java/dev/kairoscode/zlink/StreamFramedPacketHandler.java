/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

@FunctionalInterface
interface StreamFramedPacketHandler {
    void onPacket(RoutingId routingId, Message header, Message body);
}
