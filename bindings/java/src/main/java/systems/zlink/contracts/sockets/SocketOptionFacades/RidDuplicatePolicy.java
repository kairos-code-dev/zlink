/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

public enum RidDuplicatePolicy {
    REJECT(0),
    HANDOVER(1);

    private final int value;

    RidDuplicatePolicy(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static RidDuplicatePolicy fromValue(int value) {
        return switch (value) {
            case 0 -> REJECT;
            case 1 -> HANDOVER;
            default -> throw new IllegalArgumentException(
                "Invalid RidDuplicatePolicy value: " + value);
        };
    }
}
