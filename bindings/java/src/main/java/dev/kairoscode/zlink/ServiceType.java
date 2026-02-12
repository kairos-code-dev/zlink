/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum ServiceType {
    GATEWAY(1), SPOT(2);

    private final int value;
    ServiceType(int v) { this.value = v; }
    public int getValue() { return value; }
}
