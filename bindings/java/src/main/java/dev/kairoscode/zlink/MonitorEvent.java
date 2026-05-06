/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.util.Optional;

public record MonitorEvent(MonitorEventType event, long value,
                           Optional<RoutingId> routingId, String localAddr,
                           String remoteAddr) {
    public MonitorEvent {
        routingId = routingId == null ? Optional.empty() : routingId;
    }

    public Optional<ProtocolError> protocolError() {
        if (event != MonitorEventType.HANDSHAKE_FAILED_PROTOCOL) {
            return Optional.empty();
        }
        return Optional.of(ProtocolError.fromValue((int) value));
    }

    public Optional<DisconnectReason> disconnectReason() {
        if (event != MonitorEventType.DISCONNECTED) {
            return Optional.empty();
        }
        return Optional.of(DisconnectReason.fromValue((int) value));
    }
}
