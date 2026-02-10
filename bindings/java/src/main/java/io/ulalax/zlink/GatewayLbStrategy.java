/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

public enum GatewayLbStrategy {
    ROUND_ROBIN(0), WEIGHTED(1);

    private final int value;
    GatewayLbStrategy(int v) { this.value = v; }
    public int getValue() { return value; }
}
