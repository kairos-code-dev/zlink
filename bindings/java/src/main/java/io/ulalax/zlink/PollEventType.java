/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

public enum PollEventType {
    POLLIN(1), POLLOUT(2), POLLERR(4), POLLPRI(8);

    private final int value;
    PollEventType(int v) { this.value = v; }
    public int getValue() { return value; }

    public static int combine(PollEventType... flags) {
        int v = 0;
        for (var f : flags) v |= f.value;
        return v;
    }
}
