/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

public enum SpotNodeSocketType {
    ANY(0),
    PAIR(0x1001),
    PUB(0x1002),
    SUB(0x1003),
    DEALER(0x1004),
    ROUTER(0x1005),
    XPUB(0x1006),
    XSUB(0x1007),
    STREAM(0x1008);

    private final int value;

    SpotNodeSocketType(int value) {
        this.value = value;
    }

    public int getValue() {
        return value;
    }

    static SpotNodeSocketType fromValue(int value) {
        for (SpotNodeSocketType type : values()) {
            if (type.value == value)
                return type;
        }
        return ANY;
    }
}
