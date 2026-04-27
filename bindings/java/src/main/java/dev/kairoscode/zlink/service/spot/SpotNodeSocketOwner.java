/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

public enum SpotNodeSocketOwner {
    ANY(0),
    NODE(1),
    SPOT(2);

    private final int value;

    SpotNodeSocketOwner(int value) {
        this.value = value;
    }

    public int getValue() {
        return value;
    }

    static SpotNodeSocketOwner fromValue(int value) {
        for (SpotNodeSocketOwner owner : values()) {
            if (owner.value == value)
                return owner;
        }
        return ANY;
    }
}
