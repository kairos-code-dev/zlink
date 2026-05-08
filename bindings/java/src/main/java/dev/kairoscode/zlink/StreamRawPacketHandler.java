/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

@FunctionalInterface
interface StreamRawPacketHandler {
    int onPacket(RoutingId routingId, Message payload);
}
