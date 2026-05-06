/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

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

    public int value() {
        return value;
    }

    public static AutoHwmRecalcReason fromValue(int value) {
        for (AutoHwmRecalcReason reason : values()) {
            if (reason.value == value) {
                return reason;
            }
        }
        throw new IllegalArgumentException(
            "invalid AutoHwmRecalcReason value: " + value);
    }
}
