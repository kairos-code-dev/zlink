/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** How a spot peer became known to the node. */
public enum SpotPeerSource {
    /** Added manually by the application. */
    MANUAL(1),
    /** Learned from a discovery service. */
    DISCOVERY(2),
    /** Both manually added and discovered. */
    MIXED(3);

    private final int value;

    SpotPeerSource(int value) {
        this.value = value;
    }

    int getValue() {
        return value;
    }

    static SpotPeerSource fromValue(int value) {
        return switch (value) {
            case 1 -> MANUAL;
            case 2 -> DISCOVERY;
            case 3 -> MIXED;
            default -> throw invalid("SpotPeerSource", value);
        };
    }

    private static IllegalArgumentException invalid(String type, int value) {
        return new IllegalArgumentException(
            "invalid " + type + " value: " + value);
    }
}
