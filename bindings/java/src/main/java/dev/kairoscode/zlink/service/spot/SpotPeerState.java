/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.internal.EnumCodecs;

public enum SpotPeerState {
    CONFIGURED,
    CONNECTING,
    CONNECTED;

    int getValue() {
        return EnumCodecs.spotPeerStateValue(this);
    }

    static SpotPeerState fromValue(int value) {
        return EnumCodecs.spotPeerStateFromValue(value);
    }
}
