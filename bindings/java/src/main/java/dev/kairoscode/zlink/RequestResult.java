/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum RequestResult {
    OK(0),
    TIMED_OUT(101),
    NOT_FOUND(102),
    TERMINATED(103),
    PROTOCOL_ERROR(104);

    private final int value;

    RequestResult(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static RequestResult fromValue(int value) {
        for (RequestResult result : values()) {
            if (result.value == value) {
                return result;
            }
        }
        throw new IllegalArgumentException("invalid RequestResult value: " + value);
    }
}
