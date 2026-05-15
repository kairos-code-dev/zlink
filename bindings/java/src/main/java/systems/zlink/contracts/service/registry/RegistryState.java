/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


import systems.zlink.runtime.nativebridge.EnumCodecs;

public enum RegistryState {
    IDLE,
    ACTIVE,
    DEGRADED,
    ERROR;

    int getValue() {
        return EnumCodecs.registryStateValue(this);
    }

    static RegistryState fromValue(int value) {
        return EnumCodecs.registryStateFromValue(value);
    }
}
