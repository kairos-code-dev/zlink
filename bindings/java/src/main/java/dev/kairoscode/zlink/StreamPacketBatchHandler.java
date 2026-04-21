/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

@FunctionalInterface
interface StreamPacketBatchHandler {
    int onPackets(long routingId, Message[] packets);
}
