/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

public enum SpotDispatchSubjectKind {
    SPOT(1),
    TIMER(2),
    CHANNEL_DEALER(3),
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
