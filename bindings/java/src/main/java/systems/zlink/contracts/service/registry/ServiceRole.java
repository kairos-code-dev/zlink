/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


import systems.zlink.runtime.nativebridge.EnumCodecs;

public enum ServiceRole {
    INVALID,
    SPOT,
    ROUTER,
    DEALER,
    PUB,
    SUB;

    int getValue() {
        return EnumCodecs.serviceRoleValue(this);
    }

    static ServiceRole fromValue(int value) {
        return EnumCodecs.serviceRoleFromValue(value);
    }
}
