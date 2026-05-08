/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.EnumCodecs;

public enum SpotDispatchSubjectKind {
    SPOT,
    TIMER,
    CHANNEL_DEALER,
    ACTOR;

    int value() {
        return EnumCodecs.spotDispatchSubjectKindValue(this);
    }

    static SpotDispatchSubjectKind fromValue(int value) {
        return EnumCodecs.spotDispatchSubjectKindFromValue(value);
    }
}
