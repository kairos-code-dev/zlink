/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts.sockets;

import systems.zlink.contracts.errors.ConfigException;
import systems.zlink.contracts.errors.ConfigResult;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.time.Duration;
import java.util.Objects;
import java.util.Optional;

public final class RouterSocketOptions extends CommonSocketOptions {
    private static final int OPT_REQUEST_TIMEOUT_MS = 0x3105;
    private static final int OPT_WEIGHT = 0x3106;

    RouterSocketOptions(Socket socket) {
        super(socket);
    }

    public boolean mandatory() {
        return socket.getOption(SocketOptions.ROUTER_MANDATORY) != 0;
    }

    public void mandatory(boolean enabled) {
        socket.setOption(SocketOptions.ROUTER_MANDATORY, enabled ? 1 : 0);
    }

    public boolean handover() {
        return socket.options().ridDuplicatePolicy()
            == RidDuplicatePolicy.HANDOVER;
    }

    public void handover(boolean enabled) {
        socket.options().ridDuplicatePolicy(enabled
            ? RidDuplicatePolicy.HANDOVER
            : RidDuplicatePolicy.REJECT);
    }

    public boolean probe() {
        return socket.getOption(SocketOptions.PROBE_ROUTER) != 0;
    }

    public void probe(boolean enabled) {
        socket.setOption(SocketOptions.PROBE_ROUTER, enabled ? 1 : 0);
    }

    public Optional<RoutingId> connectRoutingId() {
        byte[] value = socket.getOption(SocketOptions.CONNECT_ROUTING_ID_BYTES);
        return value.length == 0 ? Optional.empty()
            : Optional.of(RoutingId.from(value));
    }

    public void connectRoutingId(RoutingId routingId) {
        socket.setOption(SocketOptions.CONNECT_ROUTING_ID_BYTES,
            routingId == null ? new byte[0] : routingId.toBytes());
    }

    public Duration requestTimeout() {
        return Duration.ofMillis(getIntOption(OPT_REQUEST_TIMEOUT_MS));
    }

    public void requestTimeout(Duration value) {
        Objects.requireNonNull(value, "value");
        setIntOption(OPT_REQUEST_TIMEOUT_MS, toIntMillis(value, "value"));
    }

    public int peerWeight() {
        return getIntOption(OPT_WEIGHT);
    }

    public void peerWeight(int value) {
        setIntOption(OPT_WEIGHT, value);
    }

    private int getIntOption(int option) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeValue = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, ValueLayout.JAVA_INT.byteSize());
            int rc = Native.getRouterOption(InternalAccess.socketHandle(socket), option, nativeValue,
              len);
            if (rc != 0)
                throw new ConfigException(ConfigResult.fromValue(rc));
            return nativeValue.get(ValueLayout.JAVA_INT, 0);
        }
    }

    private void setIntOption(int option, int value) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeValue = arena.allocate(ValueLayout.JAVA_INT);
            nativeValue.set(ValueLayout.JAVA_INT, 0, value);
            int rc = Native.setRouterOption(InternalAccess.socketHandle(socket), option, nativeValue,
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
