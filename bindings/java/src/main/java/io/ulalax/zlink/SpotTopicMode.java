/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

public enum SpotTopicMode {
    QUEUE(0), RINGBUFFER(1);

    private final int value;
    SpotTopicMode(int v) { this.value = v; }
    public int getValue() { return value; }
}
