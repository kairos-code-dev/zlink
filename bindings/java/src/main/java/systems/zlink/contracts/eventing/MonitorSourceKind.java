/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;


import systems.zlink.runtime.nativeapi.EnumCodecs;

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
