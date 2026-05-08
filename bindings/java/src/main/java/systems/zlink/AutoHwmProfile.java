/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import systems.zlink.internal.EnumCodecs;

public enum AutoHwmProfile {
    COMPACT,
    LOW_LATENCY,
    BALANCED,
    THROUGHPUT;

    int value() {
        return EnumCodecs.autoHwmProfileValue(this);
    }

    static AutoHwmProfile fromValue(int value) {
        return EnumCodecs.autoHwmProfileFromValue(value);
    }
}
