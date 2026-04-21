/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

@FunctionalInterface
interface StreamUInt32FramedPacketHandler {
    void onPacket(int routingId, Message header, Message body);
}
