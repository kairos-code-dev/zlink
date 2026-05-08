/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.internal.EnumCodecs;

public enum SpotNodeState {
    IDLE,
    CONNECTING,
    PARTIAL_READY,
    READY,
    ERROR;

    int getValue() {
        return EnumCodecs.spotNodeStateValue(this);
    }

    static SpotNodeState fromValue(int value) {
        return EnumCodecs.spotNodeStateFromValue(value);
    }
}
