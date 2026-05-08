/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.internal.EnumCodecs;

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
