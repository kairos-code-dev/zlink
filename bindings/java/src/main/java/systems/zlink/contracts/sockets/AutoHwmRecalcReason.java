/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

public enum AutoHwmRecalcReason {
    NONE(0),
    INITIAL(1),
    ROLE_CHANGE(2),
    POLICY_TOGGLE(3),
    REFRESH(4),
    DEFERRED_SHRINK(5);

    private final int value;

    AutoHwmRecalcReason(int value) {
        this.value = value;
    }

    int value() {
        return value;
    }

    static AutoHwmRecalcReason fromValue(int value) {
        return switch (value) {
            case 0 -> NONE;
            case 1 -> INITIAL;
            case 2 -> ROLE_CHANGE;
            case 3 -> POLICY_TOGGLE;
            case 4 -> REFRESH;
            case 5 -> DEFERRED_SHRINK;
            default -> throw new IllegalArgumentException(
                "Invalid AutoHwmRecalcReason value: " + value);
        };
    }
}
