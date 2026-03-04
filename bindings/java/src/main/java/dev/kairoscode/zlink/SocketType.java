/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum SocketType {
    PAIR(0), PUB(1), SUB(2), DEALER(5), ROUTER(6),
    XPUB(9), XSUB(10), STREAM(11);

    private final int value;
    SocketType(int v) { this.value = v; }
    public int getValue() { return value; }

    public static SocketType fromValue(int value) {
        for (SocketType type : values()) {
            if (type.value == value)
                return type;
        }
        return null;
    }
}
