/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

/** How a discovery service automatically wires connections between peers. */
public enum AutoConnectType {
    /** No auto-connect topology (unset). */
    INVALID(0),
    /** A mesh of ROUTER connections between peers. */
    ROUTE_MESH(1),
    /** A client-server star: clients connect to servers. */
    CLIENT_SERVER(2),
    /** A mesh of DEALER connections between peers. */
    DEALER_MESH(3),
    /** A publish/subscribe fan-out from publishers to subscribers. */
    FANOUT(4),
    /** A mesh of spot connections between peers. */
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
