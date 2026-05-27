/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;


import systems.zlink.runtime.nativeapi.EnumCodecs;

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
