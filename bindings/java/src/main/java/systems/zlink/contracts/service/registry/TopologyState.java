/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

public enum TopologyState {
    DISCOVERED(1),
    CONNECTING(2),
    READY(3),
    LOST(4),
    ERROR(5),
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
