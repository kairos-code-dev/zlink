/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum ServiceRole {
    INVALID(0),
    SPOT(2),
    ROUTER(3),
    DEALER(4),
    PUB(5),
    SUB(6);

    private final int value;

    ServiceRole(int value) {
        this.value = value;
    }

    public int getValue() {
        return value;
    }

    public static ServiceRole fromValue(int value) {
        for (ServiceRole role : values()) {
            if (role.value == value)
                return role;
        }
        throw new IllegalArgumentException("unknown ServiceRole: " + value);
    }
}
