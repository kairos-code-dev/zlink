/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** The overall readiness state of a spot node. */
public enum SpotNodeState {
    /** Not yet connecting to any peer. */
    IDLE(1),
    CONNECTING(2),
    /** Some but not all peers are connected. */
    PARTIAL_READY(3),
    /** All expected peers are connected. */
    READY(4),
    ERROR(5);

    private final int value;

    SpotNodeState(int value) {
        this.value = value;
    }

    int getValue() {
        return value;
    }

    static SpotNodeState fromValue(int value) {
        return switch (value) {
            case 1 -> IDLE;
            case 2 -> CONNECTING;
            case 3 -> PARTIAL_READY;
            case 4 -> READY;
            case 5 -> ERROR;
            default -> throw invalid("SpotNodeState", value);
        };
    }

    private static IllegalArgumentException invalid(String type, int value) {
        return new IllegalArgumentException(
            "invalid " + type + " value: " + value);
    }
}
