/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


import systems.zlink.runtime.nativebridge.EnumCodecs;

public enum PollEventFlag {
    POLLIN,
    POLLOUT,
    POLLERR,
    POLLPRI,
    POLLCOMPLETION;

    int value() {
        return EnumCodecs.pollEventFlagValue(this);
    }

    static int combine(PollEventFlag... flags) {
        return EnumCodecs.pollEventMask(flags);
    }

    static java.util.EnumSet<PollEventFlag> fromMask(int mask) {
        return EnumCodecs.pollEventFlagsFromMask(mask);
    }
}
