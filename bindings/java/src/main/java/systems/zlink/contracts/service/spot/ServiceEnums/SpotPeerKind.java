/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** The connection style of a spot peer. */
public enum SpotPeerKind {
    /** A peer in the spot mesh. */
    SPOT_MESH(1),
    /** A peer reached over a router channel. */
    ROUTER_CHANNEL(2);

    private final int value;

    SpotPeerKind(int value) {
        this.value = value;
    }

    int getValue() {
        return value;
    }

    static SpotPeerKind fromValue(int value) {
        return switch (value) {
            case 1 -> SPOT_MESH;
            case 2 -> ROUTER_CHANNEL;
            default -> throw invalid("SpotPeerKind", value);
        };
    }

    private static IllegalArgumentException invalid(String type, int value) {
        return new IllegalArgumentException(
            "invalid " + type + " value: " + value);
    }
}
