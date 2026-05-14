/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import java.util.Optional;

/** Canonical XPUB subscription event snapshot. */
public final class SubscriptionEvent {
    private RoutingId routingId;
    private String topic;
    private boolean subscribed;

    public SubscriptionEvent() {
        this(Optional.empty(), "", false);
    }

    public SubscriptionEvent(Optional<RoutingId> routingId, String topic,
                             boolean subscribed) {
        this.routingId = routingId == null ? null : routingId.orElse(null);
        this.topic = topic == null ? "" : topic;
        this.subscribed = subscribed;
    }

    public void adoptFrom(SubscriptionEvent source) {
        this.routingId = source.routingId;
        this.topic = source.topic;
        this.subscribed = source.subscribed;
    }

    public Optional<RoutingId> routingId() {
        return Optional.ofNullable(routingId);
    }

    public String topic() {
        return topic;
    }

    public boolean subscribed() {
        return subscribed;
    }
}
