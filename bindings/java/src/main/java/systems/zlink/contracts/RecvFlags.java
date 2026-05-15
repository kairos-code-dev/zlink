/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


public enum RecvFlags {
    NONE(0),
    DONT_WAIT(1);

    private final int value;

    RecvFlags(int value) {
        this.value = value;
    }

    public int value() {
        return value;
    }

    public static RecvFlags fromValue(int value) {
        return switch (value) {
            case 0 -> NONE;
            case 1 -> DONT_WAIT;
            default -> throw new IllegalArgumentException(
                "invalid RecvFlags value: " + value);
        };
    }
}
