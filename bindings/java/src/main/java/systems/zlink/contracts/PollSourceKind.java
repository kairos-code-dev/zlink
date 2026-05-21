/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

public enum PollSourceKind {
    SOCKET(1),
    FD(2),
    TIMER(3);

    private final int value;

    PollSourceKind(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    static PollSourceKind fromValue(int value) {
        return switch (value) {
            case 1 -> SOCKET;
            case 2 -> FD;
            case 3 -> TIMER;
            default -> throw new IllegalArgumentException(
                "unknown poll source kind: " + value);
        };
    }
}
