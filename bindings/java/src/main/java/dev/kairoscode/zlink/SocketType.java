/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum SocketType {
    ANY(0), PAIR(0x1001), PUB(0x1002), SUB(0x1003), DEALER(0x1004),
    ROUTER(0x1005), XPUB(0x1006), XSUB(0x1007), STREAM(0x1008);

    private final int value;
    SocketType(int v) { this.value = v; }
    public int getValue() { return value; }
    public int value() { return value; }

    public static SocketType fromValue(int value) {
        for (SocketType type : values()) {
            if (type.value == value)
                return type;
        }
        throw new IllegalArgumentException("invalid SocketType value: " + value);
    }
}
