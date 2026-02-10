/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

public enum RegistrySocketRole {
    PUB(1), ROUTER(2), PEER_SUB(3);

    private final int value;
    RegistrySocketRole(int v) { this.value = v; }
    public int getValue() { return value; }
}
