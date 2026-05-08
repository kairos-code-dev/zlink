/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

import dev.kairoscode.zlink.internal.EnumCodecs;

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
