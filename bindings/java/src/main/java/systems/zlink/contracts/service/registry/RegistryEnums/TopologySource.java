/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.registry;

/** Where a topology entry was learned from. */
public enum TopologySource {
    /** Added manually by the application. */
    MANUAL(1),
    /** Learned from a discovery service. */
    DISCOVERY(2),
    /** Learned from a service registry. */
    REGISTRY(3);

    private final int value;

    TopologySource(int value) {
        this.value = value;
    }

    int getValue() {
        return value;
    }

    static TopologySource fromValue(int value) {
        return switch (value) {
            case 1 -> MANUAL;
            case 2 -> DISCOVERY;
            case 3 -> REGISTRY;
            default -> throw invalid("TopologySource", value);
        };
    }

    private static IllegalArgumentException invalid(String type, int value) {
        return new IllegalArgumentException(
            "invalid " + type + " value: " + value);
    }
}
