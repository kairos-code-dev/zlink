/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

public enum SpotPeerState {
    CONFIGURED(1),
    CONNECTING(2),
    CONNECTED(3);

    private final int value;

    SpotPeerState(int value) {
        this.value = value;
    }

    int getValue() {
        return value;
    }

    static SpotPeerState fromValue(int value) {
        return switch (value) {
            case 1 -> CONFIGURED;
            case 2 -> CONNECTING;
            case 3 -> CONNECTED;
            default -> throw invalid("SpotPeerState", value);
        };
    }

    private static IllegalArgumentException invalid(String type, int value) {
        return new IllegalArgumentException(
            "invalid " + type + " value: " + value);
    }
}
