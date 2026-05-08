/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

import dev.kairoscode.zlink.internal.EnumCodecs;

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
