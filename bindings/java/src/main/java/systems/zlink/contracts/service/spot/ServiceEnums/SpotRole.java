/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** The pub/sub role of a spot subject. */
public enum SpotRole {
    /** A publisher subject. */
    PUB(1),
    /** A subscriber subject. */
    SUB(2);

    private final int value;

    SpotRole(int value) {
        this.value = value;
    }

    int getValue() {
        return value;
    }

    static SpotRole fromValue(int value) {
        return switch (value) {
            case 1 -> PUB;
            case 2 -> SUB;
            default -> throw invalid("SpotRole", value);
        };
    }

    private static IllegalArgumentException invalid(String type, int value) {
        return new IllegalArgumentException(
            "invalid " + type + " value: " + value);
    }
}
