/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;

/** Identifies what a monitored source is. */
public enum MonitorSourceKind {
    /** A plain socket. */
    SOCKET(1),
    /** The publish side of a spot. */
    SPOT_PUB(3),
    /** The subscribe side of a spot. */
    SPOT_SUB(4);

    private final int value;

    MonitorSourceKind(int value) {
        this.value = value;
    }

    int value() {
        return value;
    }

    private static final MonitorSourceKind[] VALUES = values();

    static MonitorSourceKind fromValue(int value) {
        for (MonitorSourceKind kind : VALUES) {
            if (kind.value == value) {
                return kind;
            }
        }
        throw new IllegalArgumentException("Invalid MonitorSourceKind value: "
            + value);
    }
}
