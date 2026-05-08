/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.spot;

import systems.zlink.internal.EnumCodecs;

public enum SpotRole {
    PUB,
    SUB;

    int getValue() {
        return EnumCodecs.spotRoleValue(this);
    }

    static SpotRole fromValue(int value) {
        return EnumCodecs.spotRoleFromValue(value);
    }
}
