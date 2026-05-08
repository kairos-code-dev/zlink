/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.internal.EnumCodecs;

public enum SpotNodeMode {
    PUBSUB,
    ROUTED,
    ALL;

    int getValue() {
        return EnumCodecs.spotNodeModeValue(this);
    }
}
