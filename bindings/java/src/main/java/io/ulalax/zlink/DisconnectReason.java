/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

public enum DisconnectReason {
    UNKNOWN(0), LOCAL(1), REMOTE(2),
    HANDSHAKE_FAILED(3), TRANSPORT_ERROR(4), CTX_TERM(5);

    private final int value;
    DisconnectReason(int v) { this.value = v; }
    public int getValue() { return value; }
}
