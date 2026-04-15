/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

public enum MonitorSourceKind {
    SOCKET(1),
    SPOT_PUB(3),
    SPOT_SUB(4),
    SPOT_NODE(5);

    private final int value;

    MonitorSourceKind(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static MonitorSourceKind fromValue(int value) {
        for (MonitorSourceKind kind : values()) {
            if (kind.value == value) {
                return kind;
            }
        }
        throw new IllegalArgumentException(
            "invalid MonitorSourceKind value: " + value);
    }
}
