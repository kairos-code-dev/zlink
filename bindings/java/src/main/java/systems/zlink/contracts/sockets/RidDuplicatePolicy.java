/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;


import systems.zlink.runtime.nativeapi.EnumCodecs;

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
