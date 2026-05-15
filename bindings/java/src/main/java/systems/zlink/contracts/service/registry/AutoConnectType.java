/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;


import systems.zlink.runtime.nativebridge.EnumCodecs;

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
