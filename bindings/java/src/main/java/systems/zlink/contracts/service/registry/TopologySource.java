/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;


import systems.zlink.runtime.nativeapi.EnumCodecs;

public enum TopologySource {
    MANUAL,
    DISCOVERY,
    REGISTRY;

    int getValue() {
        return EnumCodecs.topologySourceValue(this);
    }

    static TopologySource fromValue(int value) {
        return EnumCodecs.topologySourceFromValue(value);
    }
}
