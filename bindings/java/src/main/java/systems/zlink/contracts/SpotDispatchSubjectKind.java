/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


import systems.zlink.runtime.nativebridge.EnumCodecs;

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
