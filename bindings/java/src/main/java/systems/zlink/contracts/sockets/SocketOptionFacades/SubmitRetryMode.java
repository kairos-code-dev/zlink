/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

public enum SubmitRetryMode {
    OFF(0),
    LOCAL_FAILURE(1);

    private final int value;

    SubmitRetryMode(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static SubmitRetryMode fromValue(int value) {
        return switch (value) {
            case 0 -> OFF;
            case 1 -> LOCAL_FAILURE;
            default -> throw new IllegalArgumentException(
                "Invalid SubmitRetryMode value: " + value);
        };
    }
}
