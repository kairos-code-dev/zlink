/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

public enum SubjectKind {
    NONE(0),
    TOPIC(1),
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
