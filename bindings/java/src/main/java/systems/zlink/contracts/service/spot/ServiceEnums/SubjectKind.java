/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** How a Spot subscription subject is matched. */
public enum SubjectKind {
    /** No subject. */
    NONE(0),
    /** An exact topic match. */
    TOPIC(1),
    /** A pattern match. */
    PATTERN(2);

    private final int value;

    SubjectKind(int value) {
        this.value = value;
    }

    int getValue() {
        return value;
    }

    static SubjectKind fromValue(int value) {
        return switch (value) {
            case 0 -> NONE;
            case 1 -> TOPIC;
            case 2 -> PATTERN;
            default -> throw invalid("SubjectKind", value);
        };
    }

    private static IllegalArgumentException invalid(String type, int value) {
        return new IllegalArgumentException(
            "invalid " + type + " value: " + value);
    }
}
