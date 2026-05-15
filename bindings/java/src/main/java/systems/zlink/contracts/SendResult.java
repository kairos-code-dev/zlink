/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


enum SendResult {
    SENT(0),
    BACKPRESSURED(1),
    NOT_READY(2);

    private final int nativeValue;

    SendResult(int nativeValue) {
        this.nativeValue = nativeValue;
    }

    public int nativeValue() {
        return nativeValue;
    }

    public static SendResult fromNativeValue(int value) {
        return switch (value) {
            case 0 -> SENT;
            case 1 -> BACKPRESSURED;
            case 2 -> NOT_READY;
            default -> throw new IllegalArgumentException(
                "invalid SendResult value: " + value);
        };
    }
}
