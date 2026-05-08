/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.internal.EnumCodecs;

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
