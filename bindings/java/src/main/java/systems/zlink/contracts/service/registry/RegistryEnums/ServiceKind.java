/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

/** The kind of service a topology entry represents. */
public enum ServiceKind {
    /** A discovery service. */
    DISCOVERY(1),
    /** The subscribe side of a spot. */
    SPOT_SUB(3),
    /** The publish side of a spot. */
    SPOT_PUB(4),
    /** A plain socket. */
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
