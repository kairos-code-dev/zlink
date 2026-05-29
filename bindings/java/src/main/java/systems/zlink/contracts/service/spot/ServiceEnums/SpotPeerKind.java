/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

public enum SpotPeerKind {
    SPOT_MESH(1),
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
