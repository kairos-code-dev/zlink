/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.EnumCodecs;

public enum SocketType {
    ANY, PAIR, PUB, SUB, DEALER,
    ROUTER, XPUB, XSUB, STREAM;

    int getValue() { return EnumCodecs.socketTypeValue(this); }
    int value() { return EnumCodecs.socketTypeValue(this); }

    static SocketType fromValue(int value) {
        return EnumCodecs.socketTypeFromValue(value);
    }
}
