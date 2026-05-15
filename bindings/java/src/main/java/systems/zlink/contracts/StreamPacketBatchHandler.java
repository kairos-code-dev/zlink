/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


@FunctionalInterface
interface StreamPacketBatchHandler {
    int onPackets(long routingId, Message[] packets);
}
