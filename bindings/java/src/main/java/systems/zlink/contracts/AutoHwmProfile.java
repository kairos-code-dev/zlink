/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


import systems.zlink.runtime.nativebridge.EnumCodecs;

public enum AutoHwmProfile {
    COMPACT,
    LOW_LATENCY,
    BALANCED,
    THROUGHPUT;

    public int value() {
        return EnumCodecs.autoHwmProfileValue(this);
    }

    public static AutoHwmProfile fromValue(int value) {
        return EnumCodecs.autoHwmProfileFromValue(value);
    }
}
