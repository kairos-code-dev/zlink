/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** The kind of subject a spot dispatch event concerns. */
public enum SpotDispatchSubjectKind {
    /** The spot itself. */
    SPOT(1),
    /** A timer. */
    TIMER(2),
    /** A channel dealer socket. */
    CHANNEL_DEALER(3),
    /** An actor. */
    ACTOR(4);

    private final int value;

    SpotDispatchSubjectKind(int value) {
        this.value = value;
    }

    int value() {
        return value;
    }

    static SpotDispatchSubjectKind fromValue(int value) {
        return switch (value) {
            case 1 -> SPOT;
            case 2 -> TIMER;
            case 3 -> CHANNEL_DEALER;
            case 4 -> ACTOR;
            default -> throw new IllegalArgumentException(
                "Invalid SpotDispatchSubjectKind value: " + value);
        };
    }
}
