/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.internal.EnumCodecs;

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
