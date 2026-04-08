/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

public enum SpotRole {
    PUB(1),
    SUB(2);

    private final int value;

    SpotRole(int value) {
        this.value = value;
    }

    public int getValue() {
        return value;
    }

    public static SpotRole fromValue(int value) {
        for (SpotRole role : values()) {
            if (role.value == value) {
                return role;
            }
        }
        throw new IllegalArgumentException("unknown SpotRole: " + value);
    }
}
