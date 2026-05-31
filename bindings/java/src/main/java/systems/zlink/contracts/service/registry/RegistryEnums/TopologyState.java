/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

/** The lifecycle state of a topology connection. */
public enum TopologyState {
    /** The peer was found but a connection is not yet established. */
    DISCOVERED(1),
    CONNECTING(2),
    /** The peer is connected and usable. */
    READY(3),
    LOST(4),
    ERROR(5),
    /** The connection was explicitly stopped. */
    STOPPED(6);

    private final int value;

    TopologyState(int value) {
        this.value = value;
    }

    int getValue() {
        return value;
    }

    static TopologyState fromValue(int value) {
        return switch (value) {
            case 1 -> DISCOVERED;
            case 2 -> CONNECTING;
            case 3 -> READY;
            case 4 -> LOST;
            case 5 -> ERROR;
            case 6 -> STOPPED;
            default -> throw invalid("TopologyState", value);
        };
    }

    private static IllegalArgumentException invalid(String type, int value) {
        return new IllegalArgumentException(
            "invalid " + type + " value: " + value);
    }
}
