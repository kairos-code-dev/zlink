/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

import dev.kairoscode.zlink.internal.EnumCodecs;

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
