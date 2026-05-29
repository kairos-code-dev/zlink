/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;

import java.util.EnumSet;

public enum PollEventFlags {
    POLLIN(1),
    POLLOUT(2),
    POLLERR(4),
    POLLPRI(8),
    POLLCOMPLETION(32);

    private final int mask;

    PollEventFlags(int mask) {
        this.mask = mask;
    }

    public int mask() {
        return mask;
    }

    int value() {
        return mask();
    }

    static int combine(PollEventFlags... flags) {
        int out = 0;
        for (PollEventFlags flag : flags) {
            out |= flag.mask;
        }
        return out;
    }

    private static final PollEventFlags[] VALUES = values();

    static EnumSet<PollEventFlags> fromMask(int mask) {
        EnumSet<PollEventFlags> out = EnumSet.noneOf(PollEventFlags.class);
        for (PollEventFlags flag : VALUES) {
            if ((mask & flag.mask) != 0) {
                out.add(flag);
            }
        }
        return out;
    }
}
