/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

public enum ServiceEventSubjectKind {
    NONE(0),
    TOPIC(1),
    PATTERN(2);

    private final int value;

    ServiceEventSubjectKind(int value) {
        this.value = value;
    }

    int getValue() {
        return value;
    }

    static ServiceEventSubjectKind fromValue(int value) {
        return switch (value) {
            case 0 -> NONE;
            case 1 -> TOPIC;
            case 2 -> PATTERN;
            default -> throw invalid("ServiceEventSubjectKind", value);
        };
    }

    private static IllegalArgumentException invalid(String type, int value) {
        return new IllegalArgumentException(
            "invalid " + type + " value: " + value);
    }
}
