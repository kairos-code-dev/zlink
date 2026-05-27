/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;


import systems.zlink.runtime.nativeapi.EnumCodecs;

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
