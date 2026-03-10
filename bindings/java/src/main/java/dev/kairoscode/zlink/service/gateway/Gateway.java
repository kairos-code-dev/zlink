/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.gateway;

import dev.kairoscode.zlink.ByteSpan;
import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PeerInfo;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.options.SocketOptionKey;
import dev.kairoscode.zlink.options.SocketOptionValueType;
import dev.kairoscode.zlink.service.discovery.Discovery;
import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;

/**
 * High-level Gateway service client.
 *
 * This type is thread-affine. Use a single thread per instance, or provide
 * external synchronization for every call, including close().
 */
public final class Gateway implements AutoCloseable {
    private MemorySegment handle;
    private static final long MSG_SIZE = NativeLayouts.MSG_LAYOUT.byteSize();
    private static final int SERVICE_NAME_CAPACITY = 256;
    private static final int ROUTING_ID_LAYOUT_SIZE = 256;
    private static final int SERVICE_NAME_CACHE_LIMIT = 1024;
    private static final int ROUTING_ID_CACHE_LIMIT = 1024;
    private static final int SERVICE_NAME_SCRATCH_INITIAL_CAPACITY = 64;
    private static final int SOCKOPT_SCRATCH_INITIAL_CAPACITY = 16;
    private final ThreadLocal<MemorySegment> routingIdScratch =
      ThreadLocal.withInitial(() -> Arena.ofAuto().allocate(ROUTING_ID_LAYOUT_SIZE));
    private final ThreadLocal<MemorySegment> serviceNameScratch =
      ThreadLocal.withInitial(() -> Arena.ofAuto().allocate(
        SERVICE_NAME_SCRATCH_INITIAL_CAPACITY));
    private final ThreadLocal<Integer> serviceNameScratchCapacity =
      ThreadLocal.withInitial(() -> SERVICE_NAME_SCRATCH_INITIAL_CAPACITY);
    private final ThreadLocal<SendScratch> sendScratch =
      ThreadLocal.withInitial(SendScratch::new);
    private final ThreadLocal<RecvContext> recvScratch =
      ThreadLocal.withInitial(RecvContext::new);
    private final ConcurrentHashMap<String, MemorySegment> serviceNameCache =
      new ConcurrentHashMap<>();
    private final ConcurrentHashMap<String, MemorySegment> routingIdCache =
      new ConcurrentHashMap<>();
    private final Arena serviceNameCacheArena = Arena.ofShared();
    private final Arena routingIdCacheArena = Arena.ofShared();
    private Arena sockOptArena = Arena.ofShared();
    private MemorySegment sockOptScratch = MemorySegment.NULL;
    private int sockOptScratchCapacity = SOCKOPT_SCRATCH_INITIAL_CAPACITY;

    public Gateway(Context ctx, Discovery discovery) {
        this(ctx, discovery, null);
    }

    public Gateway(Context ctx, Discovery discovery, String routingId) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = routingId == null ? MemorySegment.NULL
                : NativeHelpers.toCString(arena, routingId);
            this.handle = Native.gatewayNew(ctx.handle(), discovery.handle(), rid);
        }
        if (handle == null || handle.address() == 0)
            throw ZlinkException.fromLastError("zlink_gateway_new");
    }

    public MemorySegment handle() {
        return handle;
    }

    public void sendTo(String serviceName, Message message, SendFlag flags) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(flags, "flags");
        Objects.requireNonNull(serviceName, "serviceName");
        MemorySegment service = serviceNameCString(serviceName);
        MemorySegment vec = sendScratch.get().ensureVector(1);
        sendSingleInternal(vec, service, message, flags, false);
    }

    public void sendTo(String serviceName, Message message) {
        sendTo(serviceName, message, SendFlag.NONE);
    }

    public void sendTo(String serviceName, Message[] messages,
                       SendFlag flags) {
        Objects.requireNonNull(flags, "flags");
        Objects.requireNonNull(serviceName, "serviceName");
        Objects.requireNonNull(messages, "messages");
        MemorySegment service = serviceNameCString(serviceName);
        int partCount = validateMessageArray(messages, "messages");
        MemorySegment vec = sendScratch.get().ensureVector(partCount);
        sendInternal(vec, service, messages, flags, false);
    }

    public void sendTo(String serviceName, Message[] messages) {
        sendTo(serviceName, messages, SendFlag.NONE);
    }

    public void sendTo(String serviceName, String routingId,
                       Message message, SendFlag flags) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(flags, "flags");
        Objects.requireNonNull(serviceName, "serviceName");
        Objects.requireNonNull(routingId, "routingId");
        MemorySegment service = serviceNameCString(serviceName);
        MemorySegment rid = buildRoutingId(routingId);
        MemorySegment vec = sendScratch.get().ensureVector(1);
        sendToRoutingIdSingleInternal(vec, service, rid, message, flags, false);
    }

    public void sendTo(String serviceName, String routingId, Message message) {
        sendTo(serviceName, routingId, message, SendFlag.NONE);
    }

    public void sendTo(String serviceName, String routingId,
                       Message[] messages, SendFlag flags) {
        Objects.requireNonNull(flags, "flags");
        Objects.requireNonNull(serviceName, "serviceName");
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(messages, "messages");
        MemorySegment service = serviceNameCString(serviceName);
        MemorySegment rid = buildRoutingId(routingId);
        int partCount = validateMessageArray(messages, "messages");
        MemorySegment vec = sendScratch.get().ensureVector(partCount);
        sendToRoutingIdInternal(vec, service, rid, messages, flags, false);
    }

    public void sendTo(String serviceName, String routingId,
                       Message[] messages) {
        sendTo(serviceName, routingId, messages, SendFlag.NONE);
    }

    private void send(String serviceName, Message[] parts, SendFlag flags) {
        Objects.requireNonNull(serviceName, "serviceName");
        MemorySegment service = serviceNameCString(serviceName);
        try (Arena arena = Arena.ofConfined()) {
            sendInternal(arena, service, parts, flags, false);
        }
    }

    private void send(PreparedService service, Message[] parts, SendFlag flags) {
        Objects.requireNonNull(service, "service");
        try (Arena arena = Arena.ofConfined()) {
            sendInternal(arena, service.cString(), parts, flags, false);
        }
    }

    private void send(PreparedService service, Message[] parts, SendFlag flags,
                     SendContext context) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(context, "context");
        sendInternal(context, service.cString(), parts, flags, false);
    }

    private void send(PreparedService service, Message part, SendFlag flags,
                     SendContext context) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(part, "part");
        Objects.requireNonNull(context, "context");
        sendSingleInternal(context, service.cString(), part, flags, false);
    }

    private void sendMove(String serviceName, Message[] parts, SendFlag flags) {
        Objects.requireNonNull(serviceName, "serviceName");
        MemorySegment service = serviceNameCString(serviceName);
        try (Arena arena = Arena.ofConfined()) {
            sendInternal(arena, service, parts, flags, true);
        }
    }

    private void sendToRoutingId(String serviceName, String routingId,
                                MemorySegment payload, SendFlag flags) {
        sendToRoutingId(serviceName, routingId, payload, 0, payload.byteSize(),
          flags);
    }

    private void sendToRoutingId(String serviceName, String routingId,
                                MemorySegment payload, long offset,
                                long length, SendFlag flags) {
        Objects.requireNonNull(serviceName, "serviceName");
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(payload, "payload");
        validatePayloadRange(payload, offset, length);
        MemorySegment service = serviceNameCString(serviceName);
        MemorySegment rid = buildRoutingId(routingId);
        sendToRoutingIdBytesInternal(service, rid, payload, offset, length,
            flags);
    }

    private void sendToRoutingId(String serviceName, byte[] routingId,
                                MemorySegment payload, SendFlag flags) {
        sendToRoutingId(serviceName, routingId, payload, 0, payload.byteSize(),
          flags);
    }

    private void sendToRoutingId(String serviceName, byte[] routingId,
                                MemorySegment payload, long offset,
                                long length, SendFlag flags) {
        Objects.requireNonNull(serviceName, "serviceName");
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(payload, "payload");
        validatePayloadRange(payload, offset, length);
        MemorySegment service = serviceNameCString(serviceName);
        MemorySegment rid = buildRoutingId(routingId);
        sendToRoutingIdBytesInternal(service, rid, payload, offset, length,
            flags);
    }

    private void sendToRoutingId(String serviceName, ByteSpan routingId,
                                MemorySegment payload, SendFlag flags) {
        sendToRoutingId(serviceName, routingId, payload, 0, payload.byteSize(),
            flags);
    }

    private void sendToRoutingId(String serviceName, ByteSpan routingId,
                                MemorySegment payload, long offset,
                                long length, SendFlag flags) {
        Objects.requireNonNull(serviceName, "serviceName");
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(payload, "payload");
        validatePayloadRange(payload, offset, length);
        MemorySegment service = serviceNameCString(serviceName);
        MemorySegment rid = buildRoutingId(routingId);
        sendToRoutingIdBytesInternal(service, rid, payload, offset, length,
            flags);
    }

    private void sendToRoutingId(PreparedService service, byte[] routingId,
                                MemorySegment payload, SendFlag flags) {
        sendToRoutingId(service, routingId, payload, 0, payload.byteSize(),
            flags);
    }

    private void sendToRoutingId(PreparedService service, String routingId,
                                MemorySegment payload, SendFlag flags) {
        sendToRoutingId(service, routingId, payload, 0, payload.byteSize(),
            flags);
    }

    private void sendToRoutingId(PreparedService service, String routingId,
                                MemorySegment payload, long offset,
                                long length, SendFlag flags) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(payload, "payload");
        validatePayloadRange(payload, offset, length);
        MemorySegment rid = buildRoutingId(routingId);
        sendToRoutingIdBytesInternal(service.cString(), rid, payload, offset,
            length, flags);
    }

    private void sendToRoutingId(PreparedService service, byte[] routingId,
                                MemorySegment payload, long offset,
                                long length, SendFlag flags) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(payload, "payload");
        validatePayloadRange(payload, offset, length);
        MemorySegment rid = buildRoutingId(routingId);
        sendToRoutingIdBytesInternal(service.cString(), rid, payload, offset,
            length, flags);
    }

    private void sendToRoutingId(PreparedService service, ByteSpan routingId,
                                MemorySegment payload, SendFlag flags) {
        sendToRoutingId(service, routingId, payload, 0, payload.byteSize(),
            flags);
    }

    private void sendToRoutingId(PreparedService service, ByteSpan routingId,
                                MemorySegment payload, long offset,
                                long length, SendFlag flags) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(payload, "payload");
        validatePayloadRange(payload, offset, length);
        MemorySegment rid = buildRoutingId(routingId);
        sendToRoutingIdBytesInternal(service.cString(), rid, payload, offset,
            length, flags);
    }

    private void sendMoveToRoutingId(String serviceName, ByteSpan routingId,
                                    Message[] parts, SendFlag flags) {
        Objects.requireNonNull(serviceName, "serviceName");
        Objects.requireNonNull(routingId, "routingId");
        MemorySegment service = serviceNameCString(serviceName);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = buildRoutingId(routingId);
            sendToRoutingIdInternal(arena, service, rid, parts, flags, true);
        }
    }

    private void sendMoveToRoutingId(String serviceName, String routingId,
                                    Message[] parts, SendFlag flags) {
        Objects.requireNonNull(serviceName, "serviceName");
        Objects.requireNonNull(routingId, "routingId");
        MemorySegment service = serviceNameCString(serviceName);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = buildRoutingId(routingId);
            sendToRoutingIdInternal(arena, service, rid, parts, flags, true);
        }
    }

    private void sendToRoutingId(String serviceName, byte[] routingId,
                                Message[] parts, SendFlag flags) {
        Objects.requireNonNull(serviceName, "serviceName");
        Objects.requireNonNull(routingId, "routingId");
        MemorySegment service = serviceNameCString(serviceName);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = buildRoutingId(routingId);
            sendToRoutingIdInternal(arena, service, rid, parts, flags, false);
        }
    }

    private void sendToRoutingId(String serviceName, String routingId,
                                 Message[] parts, SendFlag flags) {
        Objects.requireNonNull(serviceName, "serviceName");
        Objects.requireNonNull(routingId, "routingId");
        MemorySegment service = serviceNameCString(serviceName);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = buildRoutingId(routingId);
            sendToRoutingIdInternal(arena, service, rid, parts, flags, false);
        }
    }

    private void sendToRoutingId(String serviceName, ByteSpan routingId,
                                Message[] parts, SendFlag flags) {
        Objects.requireNonNull(serviceName, "serviceName");
        Objects.requireNonNull(routingId, "routingId");
        MemorySegment service = serviceNameCString(serviceName);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = buildRoutingId(routingId);
            sendToRoutingIdInternal(arena, service, rid, parts, flags, false);
        }
    }

    private void sendToRoutingId(PreparedService service, byte[] routingId,
                                Message[] parts, SendFlag flags) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(routingId, "routingId");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = buildRoutingId(routingId);
            sendToRoutingIdInternal(arena, service.cString(), rid, parts, flags,
                false);
        }
    }

    private void sendToRoutingId(PreparedService service, String routingId,
                                Message[] parts, SendFlag flags) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(routingId, "routingId");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = buildRoutingId(routingId);
            sendToRoutingIdInternal(arena, service.cString(), rid, parts, flags,
                false);
        }
    }

    private void sendToRoutingId(PreparedService service, ByteSpan routingId,
                                Message[] parts, SendFlag flags) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(routingId, "routingId");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = buildRoutingId(routingId);
            sendToRoutingIdInternal(arena, service.cString(), rid, parts, flags,
                false);
        }
    }

    private void sendToRoutingId(PreparedService service, byte[] routingId,
                                Message[] parts, SendFlag flags,
                                SendContext context) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(context, "context");
        MemorySegment rid = buildRoutingId(routingId);
        sendToRoutingIdInternal(context, service.cString(), rid, parts, flags,
            false);
    }

    private void sendToRoutingId(PreparedService service, String routingId,
                                Message[] parts, SendFlag flags,
                                SendContext context) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(context, "context");
        MemorySegment rid = buildRoutingId(routingId);
        sendToRoutingIdInternal(context, service.cString(), rid, parts, flags,
            false);
    }

    private void sendToRoutingId(PreparedService service, ByteSpan routingId,
                                Message[] parts, SendFlag flags,
                                SendContext context) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(context, "context");
        MemorySegment rid = buildRoutingId(routingId);
        sendToRoutingIdInternal(context, service.cString(), rid, parts, flags,
            false);
    }

    private void sendMoveToRoutingId(String serviceName, byte[] routingId,
                                    Message[] parts, SendFlag flags) {
        Objects.requireNonNull(serviceName, "serviceName");
        Objects.requireNonNull(routingId, "routingId");
        MemorySegment service = serviceNameCString(serviceName);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = buildRoutingId(routingId);
            sendToRoutingIdInternal(arena, service, rid, parts, flags, true);
        }
    }

    private void sendMoveToRoutingId(PreparedService service, ByteSpan routingId,
                                    Message[] parts, SendFlag flags) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(routingId, "routingId");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = buildRoutingId(routingId);
            sendToRoutingIdInternal(arena, service.cString(), rid, parts, flags,
                true);
        }
    }

    private void sendMoveToRoutingId(PreparedService service, byte[] routingId,
                                    Message[] parts, SendFlag flags) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(routingId, "routingId");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = buildRoutingId(routingId);
            sendToRoutingIdInternal(arena, service.cString(), rid, parts, flags,
                true);
        }
    }

    private void sendMoveToRoutingId(PreparedService service, String routingId,
                                    Message[] parts, SendFlag flags) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(routingId, "routingId");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = buildRoutingId(routingId);
            sendToRoutingIdInternal(arena, service.cString(), rid, parts, flags,
                true);
        }
    }

    private void sendMoveToRoutingId(PreparedService service, byte[] routingId,
                                    Message[] parts, SendFlag flags,
                                    SendContext context) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(context, "context");
        MemorySegment rid = buildRoutingId(routingId);
        sendToRoutingIdInternal(context, service.cString(), rid, parts, flags,
            true);
    }

    private void sendMoveToRoutingId(PreparedService service, String routingId,
                                    Message[] parts, SendFlag flags,
                                    SendContext context) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(context, "context");
        MemorySegment rid = buildRoutingId(routingId);
        sendToRoutingIdInternal(context, service.cString(), rid, parts, flags,
            true);
    }

    private void sendMoveToRoutingId(PreparedService service, ByteSpan routingId,
                                    Message[] parts, SendFlag flags,
                                    SendContext context) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(context, "context");
        MemorySegment rid = buildRoutingId(routingId);
        sendToRoutingIdInternal(context, service.cString(), rid, parts, flags,
            true);
    }

    private void sendMove(PreparedService service, Message[] parts, SendFlag flags) {
        Objects.requireNonNull(service, "service");
        try (Arena arena = Arena.ofConfined()) {
            sendInternal(arena, service.cString(), parts, flags, true);
        }
    }

    private void sendMove(PreparedService service, Message[] parts,
                         SendFlag flags, SendContext context) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(context, "context");
        sendInternal(context, service.cString(), parts, flags, true);
    }

    private void sendMove(PreparedService service, Message part,
                         SendFlag flags, SendContext context) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(part, "part");
        Objects.requireNonNull(context, "context");
        sendSingleInternal(context, service.cString(), part, flags, true);
    }

    public GatewayMessage recv(ReceiveFlag flags) {
        RecvContext context = recvScratch.get();
        int rc = Native.gatewayRecv(handle, context.partsPtr(), context.partCount(),
            flags.getValue(), context.serviceName());
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_gateway_recv");
        long partCount = context.partCount().get(ValueLayout.JAVA_LONG, 0);
        MemorySegment partsAddr = context.partsPtr().get(ValueLayout.ADDRESS, 0);
        Message[] parts = Message.fromMsgVector(partsAddr, partCount);
        if (parts.length != 1) {
            Message.closeAll(parts);
            throw new IllegalStateException(
                "recv expects exactly one message part; use recvMany");
        }
        String serviceName = NativeHelpers.fromCString(context.serviceName(),
            SERVICE_NAME_CAPACITY);
        return new GatewayMessage(serviceName, parts[0]);
    }

    public GatewayMessages recvMessages(ReceiveFlag flags) {
        RecvContext context = recvScratch.get();
        int rc = Native.gatewayRecv(handle, context.partsPtr(), context.partCount(),
            flags.getValue(), context.serviceName());
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_gateway_recv");
        long partCount = context.partCount().get(ValueLayout.JAVA_LONG, 0);
        MemorySegment partsAddr = context.partsPtr().get(ValueLayout.ADDRESS, 0);
        Message[] parts = Message.fromMsgVector(partsAddr, partCount);
        String serviceName = NativeHelpers.fromCString(context.serviceName(),
            SERVICE_NAME_CAPACITY);
        return new GatewayMessages(serviceName, parts);
    }

    public void setLoadBalancing(String serviceName, GatewayLbStrategy strategy) {
        int rc = Native.gatewaySetLbStrategy(handle, serviceNameCString(serviceName),
            strategy.getValue());
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_gateway_set_lb_strategy");
    }

    public void setTlsClient(String caCert, String hostname, int trustSystem) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.gatewaySetTlsClient(handle, NativeHelpers.toCString(arena, caCert),
                NativeHelpers.toCString(arena, hostname), trustSystem);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_gateway_set_tls_client");
        }
    }

    public int connectionCount(String serviceName) {
        int rc = Native.gatewayConnectionCount(handle, serviceNameCString(serviceName));
        if (rc < 0)
            throw ZlinkException.fromLastError("zlink_gateway_connection_count");
        return rc;
    }

    public Socket routerSocket() {
        throw new UnsupportedOperationException(
          "Gateway router socket handle is not exposed by the current core API.");
    }

    public List<PeerInfo> routerPeers() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            count.set(ValueLayout.JAVA_LONG, 0, 0L);
            int rc = Native.gatewayRouterPeers(handle, MemorySegment.NULL, count);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_gateway_router_peers");
            long available = count.get(ValueLayout.JAVA_LONG, 0);
            if (available <= 0)
                return Collections.emptyList();

            MemorySegment peersMem = arena.allocate(NativeLayouts.PEER_INFO_LAYOUT,
              available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = Native.gatewayRouterPeers(handle, peersMem, count);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_gateway_router_peers");

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

    public void setSockOpt(SocketOption option, byte[] value) {
        Objects.requireNonNull(option, "option");
        Objects.requireNonNull(value, "value");
        setSockOptBytes(option.getValue(), value, 0, value.length);
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

    public void setOption(SocketOptionKey<Long> option, long value) {
        Objects.requireNonNull(option, "option");
        validateOptionType(option, SocketOptionValueType.INT64);
        option.requireWritable();
        setSockOptLong(option.optionId(), value);
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

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.gatewayDestroy(handle);
        handle = MemorySegment.NULL;
        routingIdScratch.remove();
        serviceNameScratch.remove();
        serviceNameScratchCapacity.remove();
        sendScratch.remove();
        RecvContext recvContext = recvScratch.get();
        recvContext.close();
        recvScratch.remove();
        closeArena(serviceNameCacheArena);
        closeArena(routingIdCacheArena);
        closeArena(sockOptArena);
        sockOptArena = null;
        sockOptScratch = MemorySegment.NULL;
        sockOptScratchCapacity = 0;
        serviceNameCache.clear();
        routingIdCache.clear();
    }

    public static final class GatewayMessage implements AutoCloseable {
        private final String serviceName;
        private final Message message;

        GatewayMessage(String serviceName, Message message) {
            this.serviceName = serviceName == null ? "" : serviceName;
            this.message = Objects.requireNonNull(message, "message");
        }

        public String serviceName() {
            return serviceName;
        }

        public Message message() {
            return message;
        }

        @Override
        public void close() {
            message.close();
        }
    }

    private static final class PreparedService implements AutoCloseable {
        private final String serviceName;
        private Arena arena;
        private MemorySegment cString;

        PreparedService(String serviceName) {
            this.serviceName = Objects.requireNonNull(serviceName, "serviceName");
            this.arena = Arena.ofShared();
            this.cString = NativeHelpers.toCString(arena, serviceName);
        }

        public String serviceName() {
            return serviceName;
        }

        MemorySegment cString() {
            if (arena == null || !arena.scope().isAlive())
                throw new IllegalStateException("prepared service is closed");
            return cString;
        }

        @Override
        public void close() {
            if (arena != null && arena.scope().isAlive())
                arena.close();
            arena = null;
            cString = MemorySegment.NULL;
        }
    }

    private static final class SendContext implements AutoCloseable {
        private Arena arena;
        private MemorySegment vec;
        private int vecCapacity;

        SendContext() {
            this.arena = Arena.ofConfined();
            this.vec = MemorySegment.NULL;
            this.vecCapacity = 0;
        }

        void ensureOpen() {
            if (arena == null || !arena.scope().isAlive())
                throw new IllegalStateException("send context is closed");
        }

        MemorySegment ensureVector(int requiredParts) {
            ensureOpen();
            if (requiredParts <= 0)
                throw new IllegalArgumentException("parts required");
            if (vecCapacity < requiredParts) {
                vec = arena.allocate(NativeLayouts.MSG_LAYOUT, requiredParts);
                vecCapacity = requiredParts;
            }
            return vec;
        }

        @Override
        public void close() {
            if (arena != null && arena.scope().isAlive())
                arena.close();
            arena = null;
            vec = MemorySegment.NULL;
            vecCapacity = 0;
        }
    }

    private static final class SendScratch {
        private final Arena arena;
        private MemorySegment vec;
        private int vecCapacity;

        SendScratch() {
            this.arena = Arena.ofAuto();
            this.vec = MemorySegment.NULL;
            this.vecCapacity = 0;
        }

        MemorySegment ensureVector(int requiredParts) {
            if (requiredParts <= 0)
                throw new IllegalArgumentException("parts required");
            if (vecCapacity < requiredParts) {
                vec = arena.allocate(NativeLayouts.MSG_LAYOUT, requiredParts);
                vecCapacity = requiredParts;
            }
            return vec;
        }
    }

    public static final class GatewayMessages implements AutoCloseable {
        private final String serviceName;
        private final Message[] parts;
        private boolean closed;

        GatewayMessages(String serviceName, Message[] parts) {
            this.serviceName = serviceName == null ? "" : serviceName;
            this.parts = parts == null ? new Message[0] : parts;
            this.closed = false;
        }

        public String serviceName() {
            return serviceName;
        }

        public Message[] parts() {
            return parts;
        }

        @Override
        public void close() {
            if (closed)
                return;
            closed = true;
            Message.closeAll(parts);
        }
    }

    private static final class RecvContext implements AutoCloseable {
        private Arena arena;
        private final MemorySegment partsPtr;
        private final MemorySegment partCount;
        private final MemorySegment serviceName;

        RecvContext() {
            this.arena = Arena.ofShared();
            this.partsPtr = arena.allocate(ValueLayout.ADDRESS);
            this.partCount = arena.allocate(ValueLayout.JAVA_LONG);
            this.serviceName = arena.allocate(SERVICE_NAME_CAPACITY);
        }

        void ensureOpen() {
            if (arena == null || !arena.scope().isAlive())
                throw new IllegalStateException("recv context is closed");
        }

        MemorySegment partsPtr() {
            ensureOpen();
            return partsPtr;
        }

        MemorySegment partCount() {
            ensureOpen();
            return partCount;
        }

        MemorySegment serviceName() {
            ensureOpen();
            return serviceName;
        }

        @Override
        public void close() {
            if (arena != null && arena.scope().isAlive())
                arena.close();
            arena = null;
        }
    }

    private void sendInternal(Arena arena,
                              MemorySegment serviceName,
                              Message[] parts,
                              SendFlag flags,
                              boolean move) {
        if (parts == null || parts.length == 0)
            throw new IllegalArgumentException("parts required");
        MemorySegment vec = arena.allocate(NativeLayouts.MSG_LAYOUT, parts.length);
        sendInternal(vec, serviceName, parts, flags, move);
    }

    private void sendInternal(SendContext context,
                              MemorySegment serviceName,
                              Message[] parts,
                              SendFlag flags,
                              boolean move) {
        if (parts == null || parts.length == 0)
            throw new IllegalArgumentException("parts required");
        MemorySegment vec = context.ensureVector(parts.length);
        sendInternal(vec, serviceName, parts, flags, move);
    }

    private void sendInternal(MemorySegment vec,
                              MemorySegment serviceName,
                              Message[] parts,
                              SendFlag flags,
                              boolean move) {
        if (parts == null || parts.length == 0)
            throw new IllegalArgumentException("parts required");
        if (parts.length == 1) {
            sendSingleInternal(vec, serviceName, parts[0], flags, move);
            return;
        }
        int initialized = 0;
        try {
            for (int i = 0; i < parts.length; i++) {
                Message part = parts[i];
                if (part == null)
                    throw new IllegalArgumentException("parts[" + i + "] is null");
                MemorySegment dest = vec.asSlice((long) i * MSG_SIZE, MSG_SIZE);
                int rc = NativeMsg.msgInit(dest);
                if (rc != 0)
                    throw ZlinkException.fromLastError("zlink_msg_init");
                initialized++;
                if (move)
                    part.moveTo(dest);
                else
                    part.copyTo(dest);
            }
            int rc = Native.gatewaySend(handle, serviceName, vec, parts.length,
              flags.getValue());
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_gateway_send");
        } catch (RuntimeException ex) {
            closeMsgVector(vec, initialized);
            throw ex;
        }
    }

    private void sendSingleInternal(SendContext context,
                                    MemorySegment serviceName,
                                    Message part,
                                    SendFlag flags,
                                    boolean move) {
        MemorySegment vec = context.ensureVector(1);
        sendSingleInternal(vec, serviceName, part, flags, move);
    }

    private void sendSingleInternal(MemorySegment vec,
                                    MemorySegment serviceName,
                                    Message part,
                                    SendFlag flags,
                                    boolean move) {
        if (part == null)
            throw new IllegalArgumentException("part is null");
        int initialized = 0;
        try {
            int rc = NativeMsg.msgInit(vec);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_msg_init");
            initialized = 1;
            if (move)
                part.moveTo(vec);
            else
                part.copyTo(vec);
            rc = Native.gatewaySend(handle, serviceName, vec, 1,
                flags.getValue());
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_gateway_send");
        } catch (RuntimeException ex) {
            closeMsgVector(vec, initialized);
            throw ex;
        }
    }

    private void sendToRoutingIdBytesInternal(MemorySegment serviceName,
                                              MemorySegment routingId,
                                              MemorySegment payload,
                                              long offset,
                                              long length,
                                              SendFlag flags) {
        MemorySegment slice = length == 0 ? MemorySegment.NULL
          : payload.asSlice(offset, length);
        int rc = Native.gatewaySendRidBytes(handle, serviceName, routingId,
            slice, length, flags.getValue());
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_gateway_send_rid_bytes");
    }

    private void sendToRoutingIdInternal(Arena arena,
                                         MemorySegment serviceName,
                                         MemorySegment routingId,
                                         Message[] parts,
                                         SendFlag flags,
                                         boolean move) {
        if (parts == null || parts.length == 0)
            throw new IllegalArgumentException("parts required");
        MemorySegment vec = arena.allocate(NativeLayouts.MSG_LAYOUT, parts.length);
        sendToRoutingIdInternal(vec, serviceName, routingId, parts, flags, move);
    }

    private void sendToRoutingIdInternal(SendContext context,
                                         MemorySegment serviceName,
                                         MemorySegment routingId,
                                         Message[] parts,
                                         SendFlag flags,
                                         boolean move) {
        if (parts == null || parts.length == 0)
            throw new IllegalArgumentException("parts required");
        MemorySegment vec = context.ensureVector(parts.length);
        sendToRoutingIdInternal(vec, serviceName, routingId, parts, flags, move);
    }

    private void sendToRoutingIdInternal(MemorySegment vec,
                                         MemorySegment serviceName,
                                         MemorySegment routingId,
                                         Message[] parts,
                                         SendFlag flags,
                                         boolean move) {
        if (parts == null || parts.length == 0)
            throw new IllegalArgumentException("parts required");
        int initialized = 0;
        try {
            for (int i = 0; i < parts.length; i++) {
                Message part = parts[i];
                if (part == null)
                    throw new IllegalArgumentException("parts[" + i + "] is null");
                MemorySegment dest = vec.asSlice((long) i * MSG_SIZE, MSG_SIZE);
                int rc = NativeMsg.msgInit(dest);
                if (rc != 0)
                    throw ZlinkException.fromLastError("zlink_msg_init");
                initialized++;
                if (move)
                    part.moveTo(dest);
                else
                    part.copyTo(dest);
            }
            int rc = Native.gatewaySendRid(handle, serviceName, routingId, vec,
              parts.length, flags.getValue());
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_gateway_send_rid");
        } catch (RuntimeException ex) {
            closeMsgVector(vec, initialized);
            throw ex;
        }
    }

    private void sendToRoutingIdSingleInternal(MemorySegment vec,
                                               MemorySegment serviceName,
                                               MemorySegment routingId,
                                               Message part,
                                               SendFlag flags,
                                               boolean move) {
        if (part == null)
            throw new IllegalArgumentException("message is null");
        int initialized = 0;
        try {
            int rc = NativeMsg.msgInit(vec);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_msg_init");
            initialized = 1;
            if (move)
                part.moveTo(vec);
            else
                part.copyTo(vec);
            rc = Native.gatewaySendRid(handle, serviceName, routingId, vec, 1,
                flags.getValue());
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_gateway_send_rid");
        } catch (RuntimeException ex) {
            closeMsgVector(vec, initialized);
            throw ex;
        }
    }

    private static void closeMsgVector(MemorySegment vec, int count) {
        for (int i = 0; i < count; i++) {
            MemorySegment msg = vec.asSlice((long) i * MSG_SIZE, MSG_SIZE);
            NativeMsg.msgClose(msg);
        }
    }

    private MemorySegment serviceNameCString(String serviceName) {
        Objects.requireNonNull(serviceName, "serviceName");
        MemorySegment cached = serviceNameCache.get(serviceName);
        if (cached != null)
            return cached;
        if (serviceNameCache.size() >= SERVICE_NAME_CACHE_LIMIT)
            return serviceNameScratchCString(serviceName);
        MemorySegment encoded = NativeHelpers.toCString(serviceNameCacheArena,
            serviceName);
        MemorySegment previous = serviceNameCache.putIfAbsent(serviceName,
            encoded);
        return previous == null ? encoded : previous;
    }

    private MemorySegment serviceNameScratchCString(String value) {
        byte[] utf8 = value.getBytes(StandardCharsets.UTF_8);
        int needed = utf8.length + 1;
        MemorySegment scratch = serviceNameScratch.get();
        int capacity = serviceNameScratchCapacity.get();
        if (capacity < needed) {
            int grown = Math.max(capacity << 1, needed);
            scratch = Arena.ofAuto().allocate(grown);
            serviceNameScratch.set(scratch);
            serviceNameScratchCapacity.set(grown);
        }
        MemorySegment.copy(MemorySegment.ofArray(utf8), 0, scratch, 0,
            utf8.length);
        scratch.set(ValueLayout.JAVA_BYTE, utf8.length, (byte) 0);
        return scratch.asSlice(0, needed);
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

    private void setSockOptRaw(int optionId, MemorySegment value, long len) {
        int rc = Native.gatewaySetOption(handle, mapGatewayOption(optionId),
          value, len);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_gateway_set_option");
    }

    private static int mapGatewayOption(int optionId) {
        return switch (optionId) {
            case 23 -> 1;
            case 24 -> 2;
            case 28 -> 3;
            case 27 -> 4;
            case 17 -> 5;
            case 11 -> 6;
            case 12 -> 7;
            default -> throw new IllegalArgumentException(
              "unsupported Gateway socket option: " + optionId);
        };
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

    private static void validatePayloadRange(MemorySegment payload,
                                             long offset,
                                             long length) {
        if (!payload.isNative())
            throw new IllegalArgumentException(
                "payload requires a native MemorySegment");
        long total = payload.byteSize();
        if (offset < 0 || length < 0 || offset > total - length)
            throw new IndexOutOfBoundsException("payload range out of bounds");
    }

    private MemorySegment buildRoutingId(byte[] routingId) {
        Objects.requireNonNull(routingId, "routingId");
        return buildRoutingId(MemorySegment.ofArray(routingId), 0,
          routingId.length);
    }

    private MemorySegment buildRoutingId(String routingId) {
        Objects.requireNonNull(routingId, "routingId");
        MemorySegment cached = routingIdCache.get(routingId);
        if (cached != null)
            return cached;
        if (routingIdCache.size() >= ROUTING_ID_CACHE_LIMIT) {
            byte[] utf8 = routingId.getBytes(StandardCharsets.UTF_8);
            return buildRoutingId(MemorySegment.ofArray(utf8), 0, utf8.length);
        }

        byte[] utf8 = routingId.getBytes(StandardCharsets.UTF_8);
        if (utf8.length <= 0 || utf8.length > 255) {
            throw new IllegalArgumentException(
              "routingId length must be between 1 and 255");
        }

        MemorySegment rid = routingIdCacheArena.allocate(ROUTING_ID_LAYOUT_SIZE);
        rid.set(ValueLayout.JAVA_BYTE, 0, (byte) utf8.length);
        MemorySegment.copy(MemorySegment.ofArray(utf8), 0, rid, 1, utf8.length);
        MemorySegment previous = routingIdCache.putIfAbsent(routingId, rid);
        return previous == null ? rid : previous;
    }

    private MemorySegment buildRoutingId(ByteSpan routingId) {
        Objects.requireNonNull(routingId, "routingId");
        return buildRoutingId(routingId.segment(), 0, routingId.length());
    }

    private MemorySegment buildRoutingId(MemorySegment routingId,
                                         long offset,
                                         long length) {
        Objects.requireNonNull(routingId, "routingId");
        validateRange(routingId.byteSize(), offset, length, "routingId");
        if (length <= 0 || length > 255)
            throw new IllegalArgumentException(
              "routingId length must be between 1 and 255");
        MemorySegment rid = routingIdScratch.get();
        rid.set(ValueLayout.JAVA_BYTE, 0, (byte) length);
        MemorySegment.copy(routingId, offset, rid, 1, length);
        return rid;
    }

    private static void validateRange(long total, long offset, long length,
                                      String name) {
        if (offset < 0 || length < 0 || offset > total - length)
            throw new IndexOutOfBoundsException(name + " range out of bounds");
    }

    private static int validateMessageArray(Message[] messages, String name) {
        Objects.requireNonNull(messages, name);
        int size = messages.length;
        if (size <= 0)
            throw new IllegalArgumentException(name + " required");
        return size;
    }

}
