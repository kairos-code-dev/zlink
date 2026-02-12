/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum DiscoverySocketRole {
    SUB(1);

    private final int value;
    DiscoverySocketRole(int v) { this.value = v; }
    public int getValue() { return value; }
}
