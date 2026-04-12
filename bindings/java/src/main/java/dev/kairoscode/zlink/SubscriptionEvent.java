/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

/** Canonical XPUB subscription event snapshot. */
public record SubscriptionEvent(RoutingId routingId, boolean subscribed,
                                String topic) {
    public SubscriptionEvent {
        topic = topic == null ? "" : topic;
    }

    public String filter() {
        return topic;
    }
}
