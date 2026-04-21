/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

@FunctionalInterface
interface SubscribeHandler {
    void onMessage(RoutingId routingId, String topicId, Received received);
}
