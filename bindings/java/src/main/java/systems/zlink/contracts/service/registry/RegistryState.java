/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

public enum RegistryState {
    IDLE(1),
    ACTIVE(2),
    DEGRADED(3),
    ERROR(4);

    private final int value;

    RegistryState(int value) {
        this.value = value;
    }

    int getValue() {
        return value;
    }

    static RegistryState fromValue(int value) {
        return switch (value) {
            case 1 -> IDLE;
            case 2 -> ACTIVE;
            case 3 -> DEGRADED;
            case 4 -> ERROR;
            default -> throw invalid("RegistryState", value);
        };
    }

    private static IllegalArgumentException invalid(String type, int value) {
        return new IllegalArgumentException(
            "invalid " + type + " value: " + value);
    }
}
