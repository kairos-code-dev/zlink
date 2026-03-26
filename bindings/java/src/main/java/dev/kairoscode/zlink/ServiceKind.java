/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum ServiceKind {
    DISCOVERY(1),
    SPOT_SUB(3),
    SPOT_PUB(4),
    SOCKET(5);

    private final int value;

    ServiceKind(int value) {
        this.value = value;
    }

    public int getValue() {
        return value;
    }

    public static ServiceKind fromValue(int value) {
        for (ServiceKind kind : values()) {
            if (kind.value == value)
                return kind;
        }
        throw new IllegalArgumentException("unknown ServiceKind: " + value);
    }
}
