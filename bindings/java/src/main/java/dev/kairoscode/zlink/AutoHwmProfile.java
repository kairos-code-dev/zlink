/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum AutoHwmProfile {
    COMPACT(0),
    LOW_LATENCY(1),
    BALANCED(2),
    THROUGHPUT(3);

    private final int value;

    AutoHwmProfile(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static AutoHwmProfile fromValue(int value) {
        for (AutoHwmProfile profile : values()) {
            if (profile.value == value)
                return profile;
        }
        throw new IllegalArgumentException("unknown auto-HWM profile: " + value);
    }
}
