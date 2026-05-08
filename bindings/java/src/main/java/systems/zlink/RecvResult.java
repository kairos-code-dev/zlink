/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

public enum RecvResult {
    OK(0),
    NO_DATA(201),
    BUSY(202),
    TERMINATED(203),
    INVALID_HANDLE(204),
    NOT_SUPPORTED(205),
    INTERNAL_ERROR(206);

    private final int value;

    RecvResult(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static RecvResult fromValue(int value) {
        for (RecvResult result : values()) {
            if (result.value == value) {
                return result;
            }
        }
        throw new IllegalArgumentException("invalid RecvResult value: " + value);
    }
}
