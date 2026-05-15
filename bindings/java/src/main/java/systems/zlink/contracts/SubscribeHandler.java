/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


@FunctionalInterface
interface SubscribeHandler {
    void onMessage(RoutingId routingId, String topicId, Received received);
}
