/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.service.registry;

import systems.zlink.internal.EnumCodecs;

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
