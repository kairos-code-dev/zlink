/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;


import systems.zlink.runtime.nativeapi.EnumCodecs;

public enum SocketType {
    ANY, PAIR, PUB, SUB, DEALER,
    ROUTER, XPUB, XSUB, STREAM;

    public int getValue() { return EnumCodecs.socketTypeValue(this); }
    public int value() { return EnumCodecs.socketTypeValue(this); }

    public static SocketType fromValue(int value) {
        return EnumCodecs.socketTypeFromValue(value);
    }
}
