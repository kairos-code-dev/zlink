/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.receiver;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PeerInfo;
import dev.kairoscode.zlink.ReceiveFlag;
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
    private static final int ENDPOINT_CAPACITY = 256;
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

    public MemorySegment handle() {
        return handle;
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
        validateRole(role);
        setSockOpt(option, value);
    }

    public void setSockOpt(SocketOption option, byte[] value) {
        Objects.requireNonNull(option, "option");
        Objects.requireNonNull(value, "value");
        setSockOptBytes(option.getValue(), value, 0, value.length);
    }

    public void setSockOpt(ReceiverSocketRole role, SocketOption option, int value) {
        Objects.requireNonNull(role, "role");
        validateRole(role);
        setSockOpt(option, value);
    }

    public void setSockOpt(SocketOption option, int value) {
        Objects.requireNonNull(option, "option");
        setSockOptInt(option.getValue(), value);
    }

    public void setOption(SocketOptionKey<Integer> option, int value) {
        Objects.requireNonNull(option, "option");
        validateOptionType(option, SocketOptionValueType.INT32);
        option.requireWritable();
        setSockOptInt(option.optionId(), value);
    }

    public void setOption(ReceiverSocketRole role,
                          SocketOptionKey<Integer> option,
                          int value) {
        validateRole(role);
        setOption(option, value);
    }

    public void setOption(SocketOptionKey<Long> option, long value) {
        Objects.requireNonNull(option, "option");
        validateOptionType(option, SocketOptionValueType.INT64);
        option.requireWritable();
        setSockOptLong(option.optionId(), value);
    }

    public void setOption(ReceiverSocketRole role,
                          SocketOptionKey<String> option,
                          String value) {
        validateRole(role);
        setOption(option, value);
    }

    public void setOption(ReceiverSocketRole role,
                          SocketOptionKey<Long> option,
                          long value) {
        validateRole(role);
        setOption(option, value);
    }

    public void setOption(SocketOptionKey<String> option, String value) {
        Objects.requireNonNull(option, "option");
        validateOptionType(option, SocketOptionValueType.STRING);
        option.requireWritable();
        byte[] utf8 = Objects.requireNonNull(value, "value").getBytes(
          StandardCharsets.UTF_8);
        setSockOptBytes(option.optionId(), utf8, 0, utf8.length);
    }

    public void setOption(SocketOptionKey<byte[]> option, byte[] value) {
        Objects.requireNonNull(option, "option");
        validateOptionType(option, SocketOptionValueType.BYTES);
        option.requireWritable();
        Objects.requireNonNull(value, "value");
        setSockOptBytes(option.optionId(), value, 0, value.length);
    }

    public void setOption(ReceiverSocketRole role,
                          SocketOptionKey<byte[]> option,
                          byte[] value) {
        validateRole(role);
        setOption(option, value);
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

    public void setRoutingId(String routingId) {
        Objects.requireNonNull(routingId, "routingId");
        byte[] utf8 = routingId.getBytes(StandardCharsets.UTF_8);
        MemorySegment rid = ensureSockOptScratch(1 + utf8.length);
        rid.set(ValueLayout.JAVA_BYTE, 0, (byte) utf8.length);
        MemorySegment.copy(MemorySegment.ofArray(utf8), 0, rid, 1, utf8.length);
        int rc = Native.providerSetRoutingId(handle, rid, utf8.length);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_receiver_set_routing_id");
    }

    public String lastEndpoint() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment endpoint = arena.allocate(ENDPOINT_CAPACITY);
            MemorySegment size = arena.allocate(ValueLayout.JAVA_LONG);
            size.set(ValueLayout.JAVA_LONG, 0, ENDPOINT_CAPACITY);
            int rc = Native.providerLastEndpoint(handle, endpoint, size);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_receiver_last_endpoint");
            return NativeHelpers.fromCString(endpoint, ENDPOINT_CAPACITY);
        }
    }

    public ReceiverMessages recv(ReceiveFlag flags) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment partsPtr = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment partCount = arena.allocate(ValueLayout.JAVA_LONG);
            MemorySegment routingId = arena.allocate(256);
            int rc = Native.providerRecv(handle, partsPtr, partCount,
              flags.getValue(), routingId);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_receiver_recv");
            long count = partCount.get(ValueLayout.JAVA_LONG, 0);
            MemorySegment partsAddr = partsPtr.get(ValueLayout.ADDRESS, 0);
            Message[] parts = Message.fromMsgVector(partsAddr, count);
            byte ridLen = routingId.get(ValueLayout.JAVA_BYTE, 0);
            byte[] rid = new byte[Math.max(0, ridLen)];
            if (rid.length > 0) {
                MemorySegment.copy(routingId, 1, MemorySegment.ofArray(rid), 0,
                  rid.length);
            }
            return new ReceiverMessages(rid, parts);
        }
    }

    public Socket routerSocket() {
        throw new UnsupportedOperationException(
          "Receiver router socket handle is not exposed by the current core API.");
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

    private static void validateRole(ReceiverSocketRole role) {
        Objects.requireNonNull(role, "role");
        if (role != ReceiverSocketRole.ROUTER
          && role != ReceiverSocketRole.DEALER) {
            throw new IllegalArgumentException(
              "unsupported Receiver socket role: " + role);
        }
    }

    private void setSockOptRaw(int optionId, MemorySegment value, long len) {
        int rc = Native.providerSetOption(handle, mapReceiverOption(optionId),
          value, len);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_receiver_set_option");
    }

    private void setSockOptBytes(int optionId, byte[] value, int offset,
                                 int length) {
        MemorySegment buf = length == 0 ? MemorySegment.NULL
            : ensureSockOptScratch(length);
        if (length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), offset, buf, 0,
                length);
        }
        setSockOptRaw(optionId, buf, length);
    }

    private void setSockOptInt(int optionId, int value) {
        MemorySegment buf = ensureSockOptScratch(Integer.BYTES);
        buf.set(ValueLayout.JAVA_INT, 0, value);
        setSockOptRaw(optionId, buf, Integer.BYTES);
    }

    private void setSockOptLong(int optionId, long value) {
        MemorySegment buf = ensureSockOptScratch(Long.BYTES);
        buf.set(ValueLayout.JAVA_LONG, 0, value);
        setSockOptRaw(optionId, buf, Long.BYTES);
    }

    private static void closeArena(Arena arena) {
        if (arena != null && arena.scope().isAlive())
            arena.close();
    }

    private static int mapReceiverOption(int optionId) {
        return switch (optionId) {
            case 23 -> 1;
            case 24 -> 2;
            case 28 -> 3;
            case 27 -> 4;
            case 17 -> 5;
            case 11 -> 6;
            case 12 -> 7;
            default -> throw new IllegalArgumentException(
              "unsupported Receiver socket option: " + optionId);
        };
    }

    public record ReceiverResult(int status, String resolvedEndpoint, String errorMessage) {}

    public record ReceiverMessages(byte[] routingId, Message[] parts)
      implements AutoCloseable {
        public ReceiverMessages {
            routingId = routingId == null ? new byte[0] : routingId.clone();
        }

        @Override
        public void close() {
            for (Message part : parts) {
                if (part != null)
                    part.close();
            }
        }
    }
}
