/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.internal.EnumCodecs;

public enum SpotPeerSource {
    MANUAL,
    DISCOVERY,
    MIXED;

    int getValue() {
        return EnumCodecs.spotPeerSourceValue(this);
    }

    static SpotPeerSource fromValue(int value) {
        return EnumCodecs.spotPeerSourceFromValue(value);
    }
}
