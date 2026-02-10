/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

public enum GatewaySocketRole {
    ROUTER(1);

    private final int value;
    GatewaySocketRole(int v) { this.value = v; }
    public int getValue() { return value; }
}
