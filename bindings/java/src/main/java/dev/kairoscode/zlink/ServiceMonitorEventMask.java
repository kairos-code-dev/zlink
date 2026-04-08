/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

/** Typed bitmask for Discovery service monitor open calls. */
public enum ServiceMonitorEventMask {
    ERROR(1L << 4),
    DISCOVERY_SERVICE_UP(1L << 5),
    DISCOVERY_SERVICE_DOWN(1L << 6),
    DISCOVERY_PROVIDERS_CHANGED(1L << 7),
    CLOSED(1L << 17),
    ALL((1L << 4) | (1L << 5) | (1L << 6) | (1L << 7) | (1L << 17));

    private final long value;

    ServiceMonitorEventMask(long value) {
        this.value = value;
    }

    public long getValue() {
        return value;
    }

    public static long combine(ServiceMonitorEventMask... flags) {
        long value = 0L;
        for (ServiceMonitorEventMask flag : flags) {
            value |= flag.value;
        }
        return value;
    }
}
