/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

public enum ServiceRole {
    INVALID(0),
    SPOT(2),
    ROUTER(3),
    DEALER(4),
    PUB(5),
    SUB(6);

    private final int value;

    ServiceRole(int value) {
        this.value = value;
    }

    int getValue() {
        return value;
    }

    static ServiceRole fromValue(int value) {
        return switch (value) {
            case 0 -> INVALID;
            case 2 -> SPOT;
            case 3 -> ROUTER;
            case 4 -> DEALER;
            case 5 -> PUB;
            case 6 -> SUB;
            default -> throw invalid("ServiceRole", value);
        };
    }

    private static IllegalArgumentException invalid(String type, int value) {
        return new IllegalArgumentException(
            "invalid " + type + " value: " + value);
    }
}
