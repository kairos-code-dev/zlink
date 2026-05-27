/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;


import systems.zlink.runtime.nativeapi.EnumCodecs;

public enum TopologyState {
    DISCOVERED,
    CONNECTING,
    READY,
    LOST,
    ERROR,
    STOPPED;

    int getValue() {
        return EnumCodecs.topologyStateValue(this);
    }

    static TopologyState fromValue(int value) {
        return EnumCodecs.topologyStateFromValue(value);
    }
}
