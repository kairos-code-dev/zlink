/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.internal.EnumCodecs;

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
