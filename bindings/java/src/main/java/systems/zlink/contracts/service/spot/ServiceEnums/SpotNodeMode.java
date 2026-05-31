/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.service.spot;

/** Which messaging patterns a spot node enables. */
public enum SpotNodeMode {
    /** Publish/subscribe only. */
    PUBSUB(1),
    /** Routed request/reply only. */
    ROUTED(2),
    /** Both pub/sub and routed. */
    ALL(3);

    private final int value;

    SpotNodeMode(int value) {
        this.value = value;
    }

    int getValue() {
        return value;
    }
}
