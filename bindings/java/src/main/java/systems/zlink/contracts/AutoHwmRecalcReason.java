/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


import systems.zlink.runtime.nativebridge.EnumCodecs;

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
