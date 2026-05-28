/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

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

    int getValue() {
        return value;
    }

    static AutoConnectType fromValue(int value) {
        return switch (value) {
            case 0 -> INVALID;
            case 1 -> ROUTE_MESH;
            case 2 -> CLIENT_SERVER;
            case 3 -> DEALER_MESH;
            case 4 -> FANOUT;
            case 5 -> SPOT_MESH;
            default -> throw invalid("AutoConnectType", value);
        };
    }

    private static IllegalArgumentException invalid(String type, int value) {
        return new IllegalArgumentException(
            "invalid " + type + " value: " + value);
    }
}
