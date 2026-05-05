/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

public enum ActorCreateStatus {
    CREATED(1),
    EXISTING(2);

    private final int value;

    ActorCreateStatus(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static ActorCreateStatus fromValue(int value) {
        for (ActorCreateStatus status : values()) {
            if (status.value == value) {
                return status;
            }
        }
        throw new IllegalArgumentException(
          "invalid ActorCreateStatus value: " + value);
    }
}
