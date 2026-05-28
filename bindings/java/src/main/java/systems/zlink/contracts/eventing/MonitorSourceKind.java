/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.eventing;

public enum MonitorSourceKind {
    SOCKET(1),
    SPOT_PUB(3),
    SPOT_SUB(4);

    private final int value;

    MonitorSourceKind(int value) {
        this.value = value;
    }

    int value() {
        return value;
    }

    static MonitorSourceKind fromValue(int value) {
        for (MonitorSourceKind kind : values()) {
            if (kind.value == value) {
                return kind;
            }
        }
        throw new IllegalArgumentException("Invalid MonitorSourceKind value: "
            + value);
    }
}
