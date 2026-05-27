/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;

import systems.zlink.contracts.sockets.DisconnectReason;
import systems.zlink.contracts.errors.ProtocolError;
import systems.zlink.contracts.core.RoutingId;
import java.util.Optional;

public record MonitorEvent(MonitorEventType event, long value,
                           Optional<RoutingId> routingId, String localAddr,
                           String remoteAddr) {
    public MonitorEvent {
        routingId = routingId == null ? Optional.empty() : routingId;
    }

    Optional<ProtocolError> protocolError() {
        if (event != MonitorEventType.HANDSHAKE_FAILED_PROTOCOL) {
            return Optional.empty();
        }
        return Optional.of(ProtocolError.fromValue((int) value));
    }

    Optional<DisconnectReason> disconnectReason() {
        if (event != MonitorEventType.DISCONNECTED) {
            return Optional.empty();
        }
        return Optional.of(DisconnectReason.fromValue((int) value));
    }
}
