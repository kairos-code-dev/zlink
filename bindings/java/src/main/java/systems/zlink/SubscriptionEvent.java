/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import java.util.Optional;

/** Canonical XPUB subscription event snapshot. */
public record SubscriptionEvent(Optional<RoutingId> routingId,
                                String topic,
                                boolean subscribed) {
    public SubscriptionEvent {
        routingId = routingId == null ? Optional.empty() : routingId;
        topic = topic == null ? "" : topic;
    }
}
