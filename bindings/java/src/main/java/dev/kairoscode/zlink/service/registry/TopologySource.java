/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

import dev.kairoscode.zlink.internal.EnumCodecs;

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
