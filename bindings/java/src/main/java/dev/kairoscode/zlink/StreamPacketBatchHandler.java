/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.util.List;

@FunctionalInterface
public interface StreamPacketBatchHandler {
    int onPackets(int routingIdU32, List<Message> packets);
}
