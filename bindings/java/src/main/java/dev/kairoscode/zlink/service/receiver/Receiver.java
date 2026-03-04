/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.receiver;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.PeerInfo;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.options.SocketOptionKey;
import dev.kairoscode.zlink.options.SocketOptionValueType;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Objects;

public class Receiver implements AutoCloseable {
    private MemorySegment handle;
    private Arena sockOptArena = Arena.ofShared();
    private MemorySegment sockOptScratch = MemorySegment.NULL;
    private int sockOptScratchCapacity = 16;

    public Receiver(Context ctx) {
        this(ctx, null);
    }

    public Receiver(Context ctx, String routingId) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = routingId == null ? MemorySegment.NULL
                : NativeHelpers.toCString(arena, routingId);
            this.handle = Native.providerNew(ctx.handle(), rid);
        }
        if (handle == null || handle.address() == 0)
            throw ZlinkException.fromLastError("zlink_receiver_new");
    }

    public void bind(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.providerBind(handle, NativeHelpers.toCString(arena, endpoint));
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_receiver_bind");
        }
    }

    public void connectRegistry(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.providerConnectRegistry(handle, NativeHelpers.toCString(arena, endpoint));
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_receiver_connect_registry");
        }
    }

    public void register(String serviceName, String advertiseEndpoint, int weight) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.providerRegister(handle,
                NativeHelpers.toCString(arena, serviceName),
                NativeHelpers.toCString(arena, advertiseEndpoint),
                weight);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_receiver_register");
        }
    }

    public void updateWeight(String serviceName, int weight) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.providerUpdateWeight(handle,
                NativeHelpers.toCString(arena, serviceName), weight);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_receiver_update_weight");
        }
    }

    public void unregister(String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.providerUnregister(handle, NativeHelpers.toCString(arena, serviceName));
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_receiver_unregister");
        }
    }

    public void setSockOpt(ReceiverSocketRole role, SocketOption option, byte[] value) {
        Objects.requireNonNull(role, "role");
        Objects.requireNonNull(option, "option");
        Objects.requireNonNull(value, "value");
        setSockOptBytes(role.getValue(), option.getValue(), value, 0,
            value.length);
    }

    public void setSockOpt(ReceiverSocketRole role, SocketOption option, int value) {
        Objects.requireNonNull(role, "role");
        Objects.requireNonNull(option, "option");
        setSockOptInt(role.getValue(), option.getValue(), value);
    }

    public void setOption(ReceiverSocketRole role,
                          SocketOptionKey<Integer> option,
                          int value) {
        Objects.requireNonNull(role, "role");
        Objects.requireNonNull(option, "option");
        validateOptionType(option, SocketOptionValueType.INT32);
        option.requireWritable();
        setSockOptInt(role.getValue(), option.optionId(), value);
    }

    public void setOption(ReceiverSocketRole role,
                          SocketOptionKey<Long> option,
                          long value) {
        Objects.requireNonNull(role, "role");
        Objects.requireNonNull(option, "option");
        validateOptionType(option, SocketOptionValueType.INT64);
        option.requireWritable();
        setSockOptLong(role.getValue(), option.optionId(), value);
    }

    public void setOption(ReceiverSocketRole role,
                          SocketOptionKey<String> option,
                          String value) {
        Objects.requireNonNull(role, "role");
        Objects.requireNonNull(option, "option");
        validateOptionType(option, SocketOptionValueType.STRING);
        option.requireWritable();
        byte[] utf8 = Objects.requireNonNull(value, "value").getBytes(
          StandardCharsets.UTF_8);
        setSockOptBytes(role.getValue(), option.optionId(), utf8, 0,
            utf8.length);
    }

    public void setOption(ReceiverSocketRole role,
                          SocketOptionKey<byte[]> option,
                          byte[] value) {
        Objects.requireNonNull(role, "role");
        Objects.requireNonNull(option, "option");
        validateOptionType(option, SocketOptionValueType.BYTES);
        option.requireWritable();
        Objects.requireNonNull(value, "value");
        setSockOptBytes(role.getValue(), option.optionId(), value, 0,
            value.length);
    }

    public ReceiverResult registerResult(String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment status = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment resolved = arena.allocate(256);
            MemorySegment error = arena.allocate(256);
            int rc = Native.providerRegisterResult(handle, NativeHelpers.toCString(arena, serviceName),
                status, resolved, error);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_receiver_register_result");
            int st = status.get(ValueLayout.JAVA_INT, 0);
            String resolvedEp = NativeHelpers.fromCString(resolved, 256);
            String errMsg = NativeHelpers.fromCString(error, 256);
            return new ReceiverResult(st, resolvedEp, errMsg);
        }
    }

    public void setTlsServer(String cert, String key) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.providerSetTlsServer(handle, NativeHelpers.toCString(arena, cert),
                NativeHelpers.toCString(arena, key));
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_receiver_set_tls_server");
        }
    }

    public Socket routerSocket() {
        MemorySegment sock = Native.providerRouter(handle);
        if (sock == null || sock.address() == 0)
            throw ZlinkException.fromLastError("zlink_receiver_router_socket_unsafe");
        return Socket.adopt(sock, false);
    }

    public List<PeerInfo> routerPeers() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            count.set(ValueLayout.JAVA_LONG, 0, 0L);
            int rc = Native.providerRouterPeers(handle, MemorySegment.NULL,
              count);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_receiver_router_peers");
            long available = count.get(ValueLayout.JAVA_LONG, 0);
            if (available <= 0)
                return Collections.emptyList();

            MemorySegment peersMem = arena.allocate(NativeLayouts.PEER_INFO_LAYOUT,
              available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = Native.providerRouterPeers(handle, peersMem, count);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_receiver_router_peers");

            long actualLong = count.get(ValueLayout.JAVA_LONG, 0);
            if (actualLong < 0)
                actualLong = 0;
            if (actualLong > available)
                actualLong = available;
            int actual = (int) actualLong;
            long stride = NativeLayouts.PEER_INFO_LAYOUT.byteSize();
            ArrayList<PeerInfo> out = new ArrayList<>(actual);
            for (int i = 0; i < actual; i++) {
                out.add(PeerInfo.fromNative(peersMem.asSlice((long) i * stride,
                  stride)));
            }
            return out;
        }
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.providerDestroy(handle);
        handle = MemorySegment.NULL;
        closeArena(sockOptArena);
        sockOptArena = null;
        sockOptScratch = MemorySegment.NULL;
        sockOptScratchCapacity = 0;
    }

    private MemorySegment ensureSockOptScratch(int length) {
        if (length <= 0)
            return MemorySegment.NULL;
        if (sockOptScratch.address() == 0 || sockOptScratchCapacity < length) {
            closeArena(sockOptArena);
            sockOptArena = Arena.ofShared();
            sockOptScratch = sockOptArena.allocate(length);
            sockOptScratchCapacity = length;
        }
        return sockOptScratch.asSlice(0, length);
    }

    private static void validateOptionType(SocketOptionKey<?> option,
                                           SocketOptionValueType expected) {
        if (option.valueType() != expected) {
            throw new IllegalArgumentException(
              option.name() + " expects " + option.valueType()
                + ", not " + expected);
        }
    }

    private void setSockOptRaw(int role, int optionId, MemorySegment value,
                               long len) {
        int rc = Native.providerSetSockOpt(handle, role, optionId, value, len);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_receiver_setsockopt");
    }

    private void setSockOptBytes(int role, int optionId, byte[] value,
                                 int offset, int length) {
        MemorySegment buf = length == 0 ? MemorySegment.NULL
            : ensureSockOptScratch(length);
        if (length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), offset, buf, 0,
                length);
        }
        setSockOptRaw(role, optionId, buf, length);
    }

    private void setSockOptInt(int role, int optionId, int value) {
        MemorySegment buf = ensureSockOptScratch(Integer.BYTES);
        buf.set(ValueLayout.JAVA_INT, 0, value);
        setSockOptRaw(role, optionId, buf, Integer.BYTES);
    }

    private void setSockOptLong(int role, int optionId, long value) {
        MemorySegment buf = ensureSockOptScratch(Long.BYTES);
        buf.set(ValueLayout.JAVA_LONG, 0, value);
        setSockOptRaw(role, optionId, buf, Long.BYTES);
    }

    private static void closeArena(Arena arena) {
        if (arena != null && arena.scope().isAlive())
            arena.close();
    }

    public record ReceiverResult(int status, String resolvedEndpoint, String errorMessage) {}
}
