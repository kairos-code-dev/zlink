/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.registry;

public enum TopologySource {
    MANUAL(1),
    DISCOVERY(2),
    REGISTRY(3);

    private final int value;

    TopologySource(int value) {
        this.value = value;
    }

    public int getValue() {
        return value;
    }

    public static TopologySource fromValue(int value) {
        for (TopologySource source : values()) {
            if (source.value == value) {
                return source;
            }
        }
        throw new IllegalArgumentException("unknown TopologySource: " + value);
    }
}
