/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import systems.zlink.internal.EnumCodecs;

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
