/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.util.Optional;

/** Canonical XPUB subscription event snapshot. */
public record SubscriptionEvent(Optional<RoutingId> routingId, String topic,
                                Optional<String> serviceName,
                                boolean subscribed) {
    public SubscriptionEvent {
        routingId = routingId == null ? Optional.empty() : routingId;
        serviceName = serviceName == null ? Optional.empty() : serviceName;
        topic = topic == null ? "" : topic;
    }
}
