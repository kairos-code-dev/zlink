/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum SpotNodePubQueueFullPolicy {
    EAGAIN(0),
    DROP(1);

    private final int value;
    SpotNodePubQueueFullPolicy(int v) { this.value = v; }
    public int getValue() { return value; }
}
