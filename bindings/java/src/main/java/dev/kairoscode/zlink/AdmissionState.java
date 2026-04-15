/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum AdmissionState {
    SERVING(1),
    DRAINING(2);

    private final int value;

    AdmissionState(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static AdmissionState fromValue(int value) {
        for (AdmissionState state : values()) {
            if (state.value == value) {
                return state;
            }
        }
        throw new IllegalArgumentException("invalid AdmissionState value: " + value);
    }
}
