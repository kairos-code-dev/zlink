/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


import systems.zlink.runtime.nativebridge.EnumCodecs;

public enum RidDuplicatePolicy {
    REJECT,
    HANDOVER;

    public int value() {
        return EnumCodecs.ridDuplicatePolicyValue(this);
    }

    public static RidDuplicatePolicy fromValue(int value) {
        return EnumCodecs.ridDuplicatePolicyFromValue(value);
    }
}
