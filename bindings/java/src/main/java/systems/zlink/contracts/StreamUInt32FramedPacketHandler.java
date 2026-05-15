/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


@FunctionalInterface
interface StreamUInt32FramedPacketHandler {
    void onPacket(int routingId, Message header, Message body);
}
