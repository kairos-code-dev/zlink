/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;


import systems.zlink.runtime.nativebridge.EnumCodecs;

public enum SpotNodeSocketOwner {
    ANY,
    NODE,
    SPOT;

    int getValue() {
        return EnumCodecs.spotNodeSocketOwnerValue(this);
    }

    static SpotNodeSocketOwner fromValue(int value) {
        return EnumCodecs.spotNodeSocketOwnerFromValue(value);
    }
}
