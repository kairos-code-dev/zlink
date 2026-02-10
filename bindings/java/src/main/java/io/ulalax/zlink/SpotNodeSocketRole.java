/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

public enum SpotNodeSocketRole {
    PUB(1), SUB(2), DEALER(3);

    private final int value;
    SpotNodeSocketRole(int v) { this.value = v; }
    public int getValue() { return value; }
}
