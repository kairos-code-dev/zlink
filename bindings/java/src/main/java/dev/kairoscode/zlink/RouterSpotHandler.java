/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

@FunctionalInterface
public interface RouterSpotHandler {
    void onMessage(RoutingId sourceNodeRid, RoutingId sourceSpotRid,
                   long requestSeq, Received received);
}
