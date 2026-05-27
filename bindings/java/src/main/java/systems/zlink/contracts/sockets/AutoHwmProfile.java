/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;


import systems.zlink.runtime.nativeapi.EnumCodecs;

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
