/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

public enum SendFlag {
    NONE(0), DONTWAIT(1), SNDMORE(2);

    private final int value;
    SendFlag(int v) { this.value = v; }
    public int getValue() { return value; }

    public static int combine(SendFlag... flags) {
        int v = 0;
        for (var f : flags) v |= f.value;
        return v;
    }
}
