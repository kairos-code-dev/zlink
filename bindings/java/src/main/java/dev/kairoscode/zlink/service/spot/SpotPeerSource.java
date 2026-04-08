/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

public enum SpotPeerSource {
    MANUAL(1),
    DISCOVERY(2),
    MIXED(3);

    private final int value;

    SpotPeerSource(int value) {
        this.value = value;
    }

    public int getValue() {
        return value;
    }

    public static SpotPeerSource fromValue(int value) {
        for (SpotPeerSource source : values()) {
            if (source.value == value) {
                return source;
            }
        }
        throw new IllegalArgumentException("unknown SpotPeerSource: " + value);
    }
}
