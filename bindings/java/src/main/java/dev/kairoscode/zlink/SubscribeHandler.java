/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

@FunctionalInterface
public interface SubscribeHandler {
    void onMessage(RoutingId routingId, String topicId, Received received);
}
