/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

public enum ReceiveFlag {
    NONE(0), DONTWAIT(1);

    private final int value;
    ReceiveFlag(int v) { this.value = v; }
    public int getValue() { return value; }

    public static int combine(ReceiveFlag... flags) {
        int v = 0;
        for (var f : flags) v |= f.value;
        return v;
    }
}
