/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


import systems.zlink.runtime.nativebridge.EnumCodecs;

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
