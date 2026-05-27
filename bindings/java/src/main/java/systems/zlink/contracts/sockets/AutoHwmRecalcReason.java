/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;


import systems.zlink.runtime.nativeapi.EnumCodecs;

public enum AutoHwmRecalcReason {
    NONE,
    INITIAL,
    ROLE_CHANGE,
    POLICY_TOGGLE,
    REFRESH,
    DEFERRED_SHRINK;

    int value() {
        return EnumCodecs.autoHwmRecalcReasonValue(this);
    }

    static AutoHwmRecalcReason fromValue(int value) {
        return EnumCodecs.autoHwmRecalcReasonFromValue(value);
    }
}
