/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** Which component owns a spot node socket. */
public enum SpotNodeSocketOwner {
    /** Any owner (no filter). */
    ANY(0),
    /** Owned by the node itself. */
    NODE(1),
    /** Owned by a spot. */
    SPOT(2);

    private final int value;

    SpotNodeSocketOwner(int value) {
        this.value = value;
    }

    int getValue() {
        return value;
    }

    static SpotNodeSocketOwner fromValue(int value) {
        return switch (value) {
            case 0 -> ANY;
            case 1 -> NODE;
            case 2 -> SPOT;
            default -> ANY;
        };
    }
}
