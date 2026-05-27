/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;


import systems.zlink.runtime.nativeapi.EnumCodecs;

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
