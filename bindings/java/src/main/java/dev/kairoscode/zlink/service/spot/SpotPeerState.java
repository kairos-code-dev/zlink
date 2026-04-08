/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

public enum SpotPeerState {
    CONFIGURED(1),
    CONNECTING(2),
    CONNECTED(3);

    private final int value;

    SpotPeerState(int value) {
        this.value = value;
    }

    public int getValue() {
        return value;
    }

    public static SpotPeerState fromValue(int value) {
        for (SpotPeerState state : values()) {
            if (state.value == value) {
                return state;
            }
        }
        throw new IllegalArgumentException("unknown SpotPeerState: " + value);
    }
}
