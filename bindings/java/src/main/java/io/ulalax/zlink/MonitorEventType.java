/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

public enum MonitorEventType {
    CONNECTED(0x0001), CONNECT_DELAYED(0x0002),
    CONNECT_RETRIED(0x0004), LISTENING(0x0008),
    BIND_FAILED(0x0010), ACCEPTED(0x0020),
    ACCEPT_FAILED(0x0040), CLOSED(0x0080),
    CLOSE_FAILED(0x0100), DISCONNECTED(0x0200),
    MONITOR_STOPPED(0x0400),
    HANDSHAKE_FAILED_NO_DETAIL(0x0800),
    CONNECTION_READY(0x1000),
    HANDSHAKE_FAILED_PROTOCOL(0x2000),
    HANDSHAKE_FAILED_AUTH(0x4000),
    ALL(0xFFFF);

    private final int value;
    MonitorEventType(int v) { this.value = v; }
    public int getValue() { return value; }

    public static int combine(MonitorEventType... flags) {
        int v = 0;
        for (var f : flags) v |= f.value;
        return v;
    }
}
