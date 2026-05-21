/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


import systems.zlink.runtime.nativebridge.EnumCodecs;

public enum PollEventFlag {
    POLLIN,
    POLLOUT,
    POLLERR,
    POLLPRI,
    POLLCOMPLETION;

    public int mask() {
        return EnumCodecs.pollEventFlagValue(this);
    }

    int value() {
        return mask();
    }

    static int combine(PollEventFlag... flags) {
        return EnumCodecs.pollEventMask(flags);
    }

    static java.util.EnumSet<PollEventFlag> fromMask(int mask) {
        return EnumCodecs.pollEventFlagsFromMask(mask);
    }
}
