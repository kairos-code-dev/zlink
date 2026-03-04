/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum SpotNodeOption {
    PUB_MODE(1),
    PUB_QUEUE_HWM(2),
    PUB_QUEUE_FULL_POLICY(3);

    private final int value;
    SpotNodeOption(int v) { this.value = v; }
    public int getValue() { return value; }
}
