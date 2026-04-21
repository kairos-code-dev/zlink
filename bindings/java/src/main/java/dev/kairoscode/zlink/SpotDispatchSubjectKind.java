/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum SpotDispatchSubjectKind {
    SPOT(1),
    TIMER(2),
    CHANNEL_DEALER(3);

    private final int value;

    SpotDispatchSubjectKind(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static SpotDispatchSubjectKind fromValue(int value) {
        for (SpotDispatchSubjectKind kind : values()) {
            if (kind.value == value) {
                return kind;
            }
        }
        throw new IllegalArgumentException(
          "invalid SpotDispatchSubjectKind value: " + value);
    }
}
