/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


enum PollEventType {
    POLLIN(1), POLLOUT(2), POLLERR(4), POLLPRI(8);

    private final int value;
    PollEventType(int v) { this.value = v; }
    public int getValue() { return value; }

    static int combine(PollEventType... flags) {
        int v = 0;
        for (var f : flags) v |= f.value;
        return v;
    }
}
