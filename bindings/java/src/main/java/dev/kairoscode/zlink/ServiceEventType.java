/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum ServiceEventType {
    ERROR(1L << 4),
    DISCOVERY_SERVICE_UP(1L << 5),
    DISCOVERY_SERVICE_DOWN(1L << 6),
    DISCOVERY_PROVIDERS_CHANGED(1L << 7),
    CLOSED(1L << 17);

    private final long value;

    ServiceEventType(long value) {
        this.value = value;
    }

    public long getValue() {
        return value;
    }

    public static ServiceEventType fromValue(long value) {
        for (ServiceEventType type : values()) {
            if (type.value == value) {
                return type;
            }
        }
        throw new IllegalArgumentException("unknown ServiceEventType: " + value);
    }
}
