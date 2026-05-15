/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


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
        return switch (value) {
            case 0 -> OK;
            case 201 -> NO_DATA;
            case 202 -> BUSY;
            case 203 -> TERMINATED;
            case 204 -> INVALID_HANDLE;
            case 205 -> NOT_SUPPORTED;
            case 206 -> INTERNAL_ERROR;
            default -> throw new IllegalArgumentException(
                "invalid RecvResult value: " + value);
        };
    }
}
