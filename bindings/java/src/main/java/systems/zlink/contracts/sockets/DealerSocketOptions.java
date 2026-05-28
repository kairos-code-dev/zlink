/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.internal.ContractAccess;
import java.time.Duration;
import java.util.Objects;

public final class DealerSocketOptions extends CommonSocketOptions {
    private static final int OPT_REQUEST_TIMEOUT_MS = 0x3202;
    private static final int OPT_WEIGHT = 0x3203;

    public DealerSocketOptions(Socket socket) {
        super(socket);
    }

    public boolean probe() {
        return ContractAccess.socketGetOption(socket, SocketOptions.PROBE_ROUTER) != 0;
    }

    public void probe(boolean enabled) {
        ContractAccess.socketSetOption(socket, SocketOptions.PROBE_ROUTER, enabled ? 1 : 0);
    }

    public void requestTimeout(Duration value) {
        Objects.requireNonNull(value, "value");
        setIntOption(OPT_REQUEST_TIMEOUT_MS, toIntMillis(value, "value"));
    }

    public void peerWeight(int value) {
        setIntOption(OPT_WEIGHT, value);
    }

    private void setIntOption(int option, int value) {
        ContractAccess.socketSetDealerIntOption(socket, option, value);
    }

    private static int toIntMillis(Duration timeout, String name) {
        long millis = timeout.toMillis();
        if (millis < Integer.MIN_VALUE || millis > Integer.MAX_VALUE) {
            throw new IllegalArgumentException(name + " millis out of int range: "
                + millis);
        }
        return (int) millis;
    }
}
