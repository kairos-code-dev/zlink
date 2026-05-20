/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.runtime.nativebridge.EnumCodecs;

public enum SpotPeerKind {
    SPOT_MESH,
    ROUTER_CHANNEL;

    int getValue() {
        return EnumCodecs.spotPeerKindValue(this);
    }

    static SpotPeerKind fromValue(int value) {
        return EnumCodecs.spotPeerKindFromValue(value);
    }
}
