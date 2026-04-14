/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.discovery;

public enum DiscoveryDealerPeerMode {
    ROUTER(1),
    DEALER(2);

    private final int value;

    DiscoveryDealerPeerMode(int value) {
        this.value = value;
    }

    public int getValue() {
        return value;
    }

    public static DiscoveryDealerPeerMode fromValue(int value) {
        for (DiscoveryDealerPeerMode mode : values()) {
            if (mode.value == value) {
                return mode;
            }
        }
        throw new IllegalArgumentException(
          "unknown DiscoveryDealerPeerMode: " + value);
    }
}
