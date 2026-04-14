/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.util.Optional;

public record MonitorEvent(MonitorEventType event, long value,
                           Optional<RoutingId> routingId, String localAddr,
                           String remoteAddr) {
    public MonitorEvent {
        routingId = routingId == null ? Optional.empty() : routingId;
    }
}
