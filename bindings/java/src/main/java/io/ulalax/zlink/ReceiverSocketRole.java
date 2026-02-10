/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

public enum ReceiverSocketRole {
    ROUTER(1), DEALER(2);

    private final int value;
    ReceiverSocketRole(int v) { this.value = v; }
    public int getValue() { return value; }
}
