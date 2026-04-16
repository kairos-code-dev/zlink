/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

@FunctionalInterface
public interface StreamFramedPacketHandler {
    void onPacket(RoutingId routingId, Message header, Message body);
}
