/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

@FunctionalInterface
public interface StreamPacketHandler {
    int onPacket(long routingId, Message payload);
}
