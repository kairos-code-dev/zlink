/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


import systems.zlink.runtime.nativebridge.EnumCodecs;

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
