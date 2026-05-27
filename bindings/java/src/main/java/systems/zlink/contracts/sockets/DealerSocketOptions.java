/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.errors.ConfigException;
import systems.zlink.contracts.errors.ConfigResult;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.time.Duration;
import java.util.Objects;

public final class DealerSocketOptions extends CommonSocketOptions {
    private static final int OPT_REQUEST_TIMEOUT_MS = 0x3202;
    private static final int OPT_WEIGHT = 0x3203;

    DealerSocketOptions(Socket socket) {
        super(socket);
    }

    public boolean probe() {
        return socket.getOption(SocketOptions.PROBE_ROUTER) != 0;
    }

    public void probe(boolean enabled) {
        socket.setOption(SocketOptions.PROBE_ROUTER, enabled ? 1 : 0);
    }

    public void requestTimeout(Duration value) {
        Objects.requireNonNull(value, "value");
        setIntOption(OPT_REQUEST_TIMEOUT_MS, toIntMillis(value, "value"));
    }

    public void peerWeight(int value) {
        setIntOption(OPT_WEIGHT, value);
    }

    private void setIntOption(int option, int value) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeValue = arena.allocate(ValueLayout.JAVA_INT);
            nativeValue.set(ValueLayout.JAVA_INT, 0, value);
            int rc = Native.setDealerOption(InternalAccess.socketHandle(socket), option, nativeValue,
              ValueLayout.JAVA_INT.byteSize());
            if (rc != 0)
                throw new ConfigException(ConfigResult.fromValue(rc));
        }
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
