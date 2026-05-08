/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

@FunctionalInterface
public interface StreamPacketHandler {
    void onPacket(RoutingId routingId, Message header, Message body);
}
