/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum ConfigResult {
    OK(0),
    INVALID_HANDLE(701),
    INVALID_ARGUMENT(702),
    NOT_SUPPORTED(703),
    INTERNAL_ERROR(704),
    INVALID_STATE(705),
    NOT_FOUND(706);

    private final int value;

    ConfigResult(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static ConfigResult fromValue(int value) {
        for (ConfigResult result : values()) {
            if (result.value == value) {
                return result;
            }
        }
        throw new IllegalArgumentException("invalid ConfigResult value: " + value);
    }
}
