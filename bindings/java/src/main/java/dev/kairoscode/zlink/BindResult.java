/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum BindResult {
    OK(0),
    INVALID_ARGUMENT(501),
    ADDR_IN_USE(502),
    NOT_SUPPORTED(503),
    INVALID_HANDLE(504),
    INTERNAL_ERROR(505);

    private final int value;

    BindResult(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static BindResult fromValue(int value) {
        for (BindResult result : values()) {
            if (result.value == value) {
                return result;
            }
        }
        throw new IllegalArgumentException("invalid BindResult value: " + value);
    }
}
