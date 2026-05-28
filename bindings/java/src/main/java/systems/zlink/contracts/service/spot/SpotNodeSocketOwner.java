/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

public enum SpotNodeSocketOwner {
    ANY(0),
    NODE(1),
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
