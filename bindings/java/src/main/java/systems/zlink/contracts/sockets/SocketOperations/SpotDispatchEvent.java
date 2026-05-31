/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

/** The kind of readable event surfaced by a spot dispatch. */
public enum SpotDispatchEvent {
    /** A subscription has a message ready to read. */
    SUBSCRIBE_READABLE(1),
    /** A routed message is ready to read. */
    ROUTED_READABLE(2),
    /** A timer has expired. */
    TIMER_READABLE(3),
    /** A channel reply is ready to read. */
    CHANNEL_REPLY_READABLE(4),
    /** An actor message is ready to read. */
    ACTOR_READABLE(5),
    /** An actor join request is ready to read. */
    ACTOR_JOIN_READABLE(6),
    /** An actor lifecycle event is ready to read. */
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
