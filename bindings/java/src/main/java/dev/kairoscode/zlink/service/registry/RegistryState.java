/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

public enum RegistryState {
    IDLE(1),
    ACTIVE(2),
    DEGRADED(3),
    ERROR(4);

    private final int value;

    RegistryState(int value) {
        this.value = value;
    }

    public int getValue() {
        return value;
    }

    public static RegistryState fromValue(int value) {
        for (RegistryState state : values()) {
            if (state.value == value) {
                return state;
            }
        }
        throw new IllegalArgumentException("unknown RegistryState: " + value);
    }
}
