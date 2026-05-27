/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;


import systems.zlink.runtime.nativeapi.EnumCodecs;

public enum SpotDispatchEvent {
    SUBSCRIBE_READABLE,
    ROUTED_READABLE,
    TIMER_READABLE,
    CHANNEL_REPLY_READABLE,
    ACTOR_READABLE,
    ACTOR_JOIN_READABLE;

    int value() {
        return EnumCodecs.spotDispatchEventValue(this);
    }

    static SpotDispatchEvent fromValue(int value) {
        return EnumCodecs.spotDispatchEventFromValue(value);
    }
}
