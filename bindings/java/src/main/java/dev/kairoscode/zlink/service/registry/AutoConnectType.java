/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

import dev.kairoscode.zlink.internal.EnumCodecs;

public enum AutoConnectType {
    INVALID,
    ROUTE_MESH,
    CLIENT_SERVER,
    DEALER_MESH,
    FANOUT,
    SPOT_MESH;

    int getValue() {
        return EnumCodecs.autoConnectTypeValue(this);
    }

    static AutoConnectType fromValue(int value) {
        return EnumCodecs.autoConnectTypeFromValue(value);
    }
}
