/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

public enum SpotActorLifecycleEventKind {
    JOINED,
    LEFT;

    static SpotActorLifecycleEventKind fromValue(int value) {
        return switch (value) {
            case 1 -> JOINED;
            case 2 -> LEFT;
            default -> throw new IllegalArgumentException(
              "invalid SpotActorLifecycleEventKind value: " + value);
        };
    }
}
