/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.internal.EnumCodecs;

public enum SpotPeerSource {
    MANUAL,
    DISCOVERY,
    MIXED;

    int getValue() {
        return EnumCodecs.spotPeerSourceValue(this);
    }

    static SpotPeerSource fromValue(int value) {
        return EnumCodecs.spotPeerSourceFromValue(value);
    }
}
