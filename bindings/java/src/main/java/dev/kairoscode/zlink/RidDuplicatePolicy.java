/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.EnumCodecs;

public enum RidDuplicatePolicy {
    REJECT,
    HANDOVER;

    int value() {
        return EnumCodecs.ridDuplicatePolicyValue(this);
    }

    static RidDuplicatePolicy fromValue(int value) {
        return EnumCodecs.ridDuplicatePolicyFromValue(value);
    }
}
