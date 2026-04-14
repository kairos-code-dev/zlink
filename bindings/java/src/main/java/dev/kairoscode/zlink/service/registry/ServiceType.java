/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

public enum ServiceType {
    SPOT(0x3002),
    SOCKET(0x3003);

    private final int value;
    ServiceType(int v) { this.value = v; }
    public int getValue() { return value; }

    public static ServiceType fromValue(int value) {
        for (ServiceType type : values()) {
            if (type.value == value)
                return type;
        }
        throw new IllegalArgumentException("unknown ServiceType: " + value);
    }
}
