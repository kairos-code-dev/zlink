/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** The kind of an actor lifecycle transition event. */
public enum SpotActorLifecycleEventKind {
    JOINED,
    LEFT,
    DISCONNECTED;

    static SpotActorLifecycleEventKind fromValue(int value) {
        return switch (value) {
            case 1 -> JOINED;
            case 2 -> LEFT;
            case 3 -> DISCONNECTED;
            default -> throw new IllegalArgumentException(
              "invalid SpotActorLifecycleEventKind value: " + value);
        };
    }
}
