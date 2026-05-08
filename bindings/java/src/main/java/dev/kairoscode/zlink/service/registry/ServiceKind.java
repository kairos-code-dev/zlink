/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

import dev.kairoscode.zlink.internal.EnumCodecs;

public enum ServiceKind {
    DISCOVERY,
    SPOT_SUB,
    SPOT_PUB,
    SOCKET;

    int getValue() {
        return EnumCodecs.serviceKindValue(this);
    }

    static ServiceKind fromValue(int value) {
        return EnumCodecs.serviceKindFromValue(value);
    }
}
