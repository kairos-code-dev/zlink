/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


import systems.zlink.runtime.nativebridge.EnumCodecs;

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
