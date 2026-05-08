/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

@FunctionalInterface
interface StreamPacketBatchHandler {
    int onPackets(long routingId, Message[] packets);
}
