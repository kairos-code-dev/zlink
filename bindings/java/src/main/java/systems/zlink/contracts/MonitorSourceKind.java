/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


import systems.zlink.runtime.nativebridge.EnumCodecs;

public enum MonitorSourceKind {
    SOCKET,
    SPOT_PUB,
    SPOT_SUB;

    int value() {
        return EnumCodecs.monitorSourceKindValue(this);
    }

    static MonitorSourceKind fromValue(int value) {
        return EnumCodecs.monitorSourceKindFromValue(value);
    }
}
