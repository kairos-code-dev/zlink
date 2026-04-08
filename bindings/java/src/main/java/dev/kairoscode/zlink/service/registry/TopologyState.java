/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

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

    public int getValue() {
        return value;
    }

    public static TopologyState fromValue(int value) {
        for (TopologyState state : values()) {
            if (state.value == value) {
                return state;
            }
        }
        throw new IllegalArgumentException("unknown TopologyState: " + value);
    }
}
