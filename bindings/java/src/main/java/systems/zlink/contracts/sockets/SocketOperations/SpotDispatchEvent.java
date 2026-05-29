/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

public enum SpotDispatchEvent {
    SUBSCRIBE_READABLE(1),
    ROUTED_READABLE(2),
    TIMER_READABLE(3),
    CHANNEL_REPLY_READABLE(4),
    ACTOR_READABLE(5),
    ACTOR_JOIN_READABLE(6),
    ACTOR_LIFECYCLE_READABLE(7);

    private final int value;

    SpotDispatchEvent(int value) {
        this.value = value;
    }

    int value() {
        return value;
    }

    static SpotDispatchEvent fromValue(int value) {
        return switch (value) {
            case 1 -> SUBSCRIBE_READABLE;
            case 2 -> ROUTED_READABLE;
            case 3 -> TIMER_READABLE;
            case 4 -> CHANNEL_REPLY_READABLE;
            case 5 -> ACTOR_READABLE;
            case 6 -> ACTOR_JOIN_READABLE;
            case 7 -> ACTOR_LIFECYCLE_READABLE;
            default -> throw new IllegalArgumentException(
                "Invalid SpotDispatchEvent value: " + value);
        };
    }
}
