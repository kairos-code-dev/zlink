/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum HandlerResult {
    OK(0),
    INVALID_ARGUMENT(301),
    BUSY(302),
    NOT_SUPPORTED(303),
    DEADLOCK(304),
    INVALID_HANDLE(305),
    INTERNAL_ERROR(306);

    private final int value;

    HandlerResult(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static HandlerResult fromValue(int value) {
        for (HandlerResult result : values()) {
            if (result.value == value) {
                return result;
            }
        }
        throw new IllegalArgumentException("invalid HandlerResult value: " + value);
    }
}
