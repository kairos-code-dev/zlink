/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.errors;


enum ProtocolError {
    ZMP_MALFORMED_COMMAND_HELLO(0x10000013);

    private final int value;
    ProtocolError(int v) { this.value = v; }
    public int getValue() { return value; }
    public int value() { return value; }

    public static ProtocolError fromValue(int value) {
        for (ProtocolError error : values()) {
            if (error.value == value) {
                return error;
            }
        }
        throw new IllegalArgumentException("invalid ProtocolError value: " + value);
    }
}
