/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

public enum ServiceKind {
    DISCOVERY(1),
    SPOT_SUB(3),
    SPOT_PUB(4),
    SOCKET(5);

    private final int value;

    ServiceKind(int value) {
        this.value = value;
    }

    int getValue() {
        return value;
    }

    static ServiceKind fromValue(int value) {
        return switch (value) {
            case 1 -> DISCOVERY;
            case 3 -> SPOT_SUB;
            case 4 -> SPOT_PUB;
            case 5 -> SOCKET;
            default -> throw invalid("ServiceKind", value);
        };
    }

    private static IllegalArgumentException invalid(String type, int value) {
        return new IllegalArgumentException(
            "invalid " + type + " value: " + value);
    }
}
