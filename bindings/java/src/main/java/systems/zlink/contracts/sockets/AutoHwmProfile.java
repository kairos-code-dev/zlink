/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

public enum AutoHwmProfile {
    COMPACT(0),
    LOW_LATENCY(1),
    BALANCED(2),
    THROUGHPUT(3);

    private final int value;

    AutoHwmProfile(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static AutoHwmProfile fromValue(int value) {
        return switch (value) {
            case 0 -> COMPACT;
            case 1 -> LOW_LATENCY;
            case 2 -> BALANCED;
            case 3 -> THROUGHPUT;
            default -> throw new IllegalArgumentException(
                "Invalid AutoHwmProfile value: " + value);
        };
    }
}
