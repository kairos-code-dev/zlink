/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

public enum AutoConnectType {
    INVALID(0),
    ROUTE_MESH(1),
    CLIENT_SERVER(2),
    DEALER_MESH(3),
    FANOUT(4),
    SPOT_MESH(5);

    private final int value;

    AutoConnectType(int value) {
        this.value = value;
    }

    public int getValue() {
        return value;
    }

    public static AutoConnectType fromValue(int value) {
        for (AutoConnectType type : values()) {
            if (type.value == value)
                return type;
        }
        throw new IllegalArgumentException("unknown AutoConnectType: " + value);
    }
}
