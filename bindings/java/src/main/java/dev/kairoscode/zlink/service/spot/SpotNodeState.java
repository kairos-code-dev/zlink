/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

public enum SpotNodeState {
    IDLE(1),
    CONNECTING(2),
    PARTIAL_READY(3),
    READY(4),
    ERROR(5);

    private final int value;

    SpotNodeState(int value) {
        this.value = value;
    }

    public int getValue() {
        return value;
    }

    public static SpotNodeState fromValue(int value) {
        for (SpotNodeState state : values()) {
            if (state.value == value) {
                return state;
            }
        }
        throw new IllegalArgumentException("unknown SpotNodeState: " + value);
    }
}
