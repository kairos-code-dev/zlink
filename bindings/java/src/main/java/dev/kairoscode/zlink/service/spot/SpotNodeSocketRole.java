/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;


public enum SpotNodeSocketRole {
    NODE(0), PUB(1), SUB(2), DEALER(3);

    private final int value;
    SpotNodeSocketRole(int v) { this.value = v; }
    public int getValue() { return value; }
}
