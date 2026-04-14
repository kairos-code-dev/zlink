/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

enum ProtocolError {
    ZMP_MALFORMED_COMMAND_HELLO(0x10000013);

    private final int value;
    ProtocolError(int v) { this.value = v; }
    public int getValue() { return value; }
}
