/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum SpotNodePubMode {
    SYNC(0),
    ASYNC(1);

    private final int value;
    SpotNodePubMode(int v) { this.value = v; }
    public int getValue() { return value; }
}
