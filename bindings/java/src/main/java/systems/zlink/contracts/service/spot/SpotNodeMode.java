/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

public enum SpotNodeMode {
    PUBSUB(1),
    ROUTED(2),
    ALL(3);

    private final int value;

    SpotNodeMode(int value) {
        this.value = value;
    }

    int getValue() {
        return value;
    }
}
