/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;

import java.util.EnumSet;

public enum PollEventFlag {
    POLLIN(1),
    POLLOUT(2),
    POLLERR(4),
    POLLPRI(8),
    POLLCOMPLETION(32);

    private final int mask;

    PollEventFlag(int mask) {
        this.mask = mask;
    }

    public int mask() {
        return mask;
    }

    int value() {
        return mask();
    }

    static int combine(PollEventFlag... flags) {
        int out = 0;
        for (PollEventFlag flag : flags) {
            out |= flag.mask;
        }
        return out;
    }

    static EnumSet<PollEventFlag> fromMask(int mask) {
        EnumSet<PollEventFlag> out = EnumSet.noneOf(PollEventFlag.class);
        for (PollEventFlag flag : values()) {
            if ((mask & flag.mask) != 0) {
                out.add(flag);
            }
        }
        return out;
    }
}
