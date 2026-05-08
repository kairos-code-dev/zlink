/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import systems.zlink.internal.EnumCodecs;

public enum MonitorEventType {
    CONNECTED, CONNECT_DELAYED,
    CONNECT_RETRIED, LISTENING,
    BIND_FAILED, ACCEPTED,
    ACCEPT_FAILED, CLOSED,
    CLOSE_FAILED, DISCONNECTED,
    MONITOR_STOPPED,
    HANDSHAKE_FAILED_NO_DETAIL,
    CONNECTION_READY,
    HANDSHAKE_FAILED_PROTOCOL,
    HANDSHAKE_FAILED_AUTH,
    PEER_WEIGHT_CHANGED,
    ALL;

    int getValue() { return EnumCodecs.monitorEventTypeValue(this); }

    static MonitorEventType fromValue(long value) {
        return EnumCodecs.monitorEventTypeFromValue(value);
    }

    static int combine(MonitorEventType... flags) {
        return EnumCodecs.monitorEventMask(flags);
    }
}
