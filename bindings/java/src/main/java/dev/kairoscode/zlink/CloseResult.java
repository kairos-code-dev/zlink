/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum CloseResult {
    OK(0),
    BUSY(401),
    SHUTDOWN(402),
    INVALID_HANDLE(403),
    INTERNAL_ERROR(404);

    private final int value;

    CloseResult(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static CloseResult fromValue(int value) {
        for (CloseResult result : values()) {
            if (result.value == value) {
                return result;
            }
        }
        throw new IllegalArgumentException("invalid CloseResult value: " + value);
    }
}
