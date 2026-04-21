/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum SpotDispatchEvent {
    SUBSCRIBE_READABLE(1),
    ROUTED_READABLE(2),
    TIMER_READABLE(3),
    CHANNEL_REPLY_READABLE(4);

    private final int value;

    SpotDispatchEvent(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static SpotDispatchEvent fromValue(int value) {
        for (SpotDispatchEvent event : values()) {
            if (event.value == value) {
                return event;
            }
        }
        throw new IllegalArgumentException("invalid SpotDispatchEvent value: " + value);
    }
}
