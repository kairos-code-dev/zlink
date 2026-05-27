/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;


public enum StreamDispatchMode {
    NONE(0),
    LEN32BE(0x0001);

    private final int value;

    StreamDispatchMode(int value) {
        this.value = value;
    }

    public int getValue() {
        return value;
    }
}
