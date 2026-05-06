/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum RidDuplicatePolicy {
    REJECT(0),
    HANDOVER(1);

    private final int value;

    RidDuplicatePolicy(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static RidDuplicatePolicy fromValue(int value) {
        for (RidDuplicatePolicy policy : values()) {
            if (policy.value == value) {
                return policy;
            }
        }
        throw new IllegalArgumentException(
            "invalid RidDuplicatePolicy value: " + value);
    }
}
