/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

/** The messaging role a service plays in the topology. */
public enum ServiceRole {
    /** No role (unset). */
    INVALID(0),
    /** A spot. */
    SPOT(2),
    /** A ROUTER endpoint. */
    ROUTER(3),
    /** A DEALER endpoint. */
    DEALER(4),
    /** A publisher. */
    PUB(5),
    /** A subscriber. */
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
