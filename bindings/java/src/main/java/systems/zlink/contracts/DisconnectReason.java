/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


enum DisconnectReason {
    UNKNOWN(0),
    HANDSHAKE_FAILED(3), TRANSPORT_ERROR(4), CTX_TERM(5);

    private final int value;
    DisconnectReason(int v) { this.value = v; }
    public int getValue() { return value; }
    public int value() { return value; }

    public static DisconnectReason fromValue(int value) {
        for (DisconnectReason reason : values()) {
            if (reason.value == value) {
                return reason;
            }
        }
        throw new IllegalArgumentException(
            "invalid DisconnectReason value: " + value);
    }
}
