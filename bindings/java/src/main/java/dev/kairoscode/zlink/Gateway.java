/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public final class Gateway implements AutoCloseable {
    private MemorySegment handle;
    private static final long MSG_SIZE = NativeLayouts.MSG_LAYOUT.byteSize();
    private static final int SERVICE_NAME_CAPACITY = 256;

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
            throw new RuntimeException("zlink_gateway_new failed");
    }

    public void send(String serviceName, Message[] parts, SendFlag flags) {
        sendInternal(serviceName, parts, flags, false);
    }

    public void sendMove(String serviceName, Message[] parts, SendFlag flags) {
        sendInternal(serviceName, parts, flags, true);
    }

    public GatewayMessage recv(ReceiveFlag flags) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment partsPtr = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            MemorySegment service = arena.allocate(SERVICE_NAME_CAPACITY);
            int rc = Native.gatewayRecv(handle, partsPtr, count, flags.getValue(), service);
            if (rc != 0)
                throw new RuntimeException("zlink_gateway_recv failed");
            long partCount = count.get(ValueLayout.JAVA_LONG, 0);
            MemorySegment partsAddr = partsPtr.get(ValueLayout.ADDRESS, 0);
            byte[][] data = NativeMsg.readMsgVector(partsAddr, partCount);
            String serviceName = NativeHelpers.fromCString(service, SERVICE_NAME_CAPACITY);
            return new GatewayMessage(serviceName, data);
        }
    }

    public GatewayMessages recvMessages(ReceiveFlag flags) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment partsPtr = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            MemorySegment service = arena.allocate(SERVICE_NAME_CAPACITY);
            int rc = Native.gatewayRecv(handle, partsPtr, count, flags.getValue(), service);
            if (rc != 0)
                throw new RuntimeException("zlink_gateway_recv failed");
            long partCount = count.get(ValueLayout.JAVA_LONG, 0);
            MemorySegment partsAddr = partsPtr.get(ValueLayout.ADDRESS, 0);
            Message[] parts = Message.fromMsgVector(partsAddr, partCount);
            String serviceName = NativeHelpers.fromCString(service, SERVICE_NAME_CAPACITY);
            return new GatewayMessages(serviceName, parts);
        }
    }

    public void setLoadBalancing(String serviceName, GatewayLbStrategy strategy) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.gatewaySetLbStrategy(handle, NativeHelpers.toCString(arena, serviceName), strategy.getValue());
            if (rc != 0)
                throw new RuntimeException("zlink_gateway_set_lb_strategy failed");
        }
    }

    public void setTlsClient(String caCert, String hostname, int trustSystem) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.gatewaySetTlsClient(handle, NativeHelpers.toCString(arena, caCert),
                NativeHelpers.toCString(arena, hostname), trustSystem);
            if (rc != 0)
                throw new RuntimeException("zlink_gateway_set_tls_client failed");
        }
    }

    public int connectionCount(String serviceName) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.gatewayConnectionCount(handle, NativeHelpers.toCString(arena, serviceName));
            if (rc < 0)
                throw new RuntimeException("zlink_gateway_connection_count failed");
            return rc;
        }
    }

    public void setSockOpt(SocketOption option, byte[] value) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(value.length);
            MemorySegment.copy(MemorySegment.ofArray(value), 0, buf, 0, value.length);
            int rc = Native.gatewaySetSockOpt(handle, option.getValue(), buf, value.length);
            if (rc != 0)
                throw new RuntimeException("zlink_gateway_setsockopt failed");
        }
    }

    public void setSockOpt(SocketOption option, int value) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(ValueLayout.JAVA_INT);
            buf.set(ValueLayout.JAVA_INT, 0, value);
            int rc = Native.gatewaySetSockOpt(handle, option.getValue(), buf, ValueLayout.JAVA_INT.byteSize());
            if (rc != 0)
                throw new RuntimeException("zlink_gateway_setsockopt failed");
        }
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.gatewayDestroy(handle);
        handle = MemorySegment.NULL;
    }

    public record GatewayMessage(String serviceName, byte[][] parts) {}

    public static final class GatewayMessages implements AutoCloseable {
        private final String serviceName;
        private final Message[] parts;

        GatewayMessages(String serviceName, Message[] parts) {
            this.serviceName = serviceName == null ? "" : serviceName;
            this.parts = parts == null ? new Message[0] : parts;
        }

        public String serviceName() {
            return serviceName;
        }

        public Message[] parts() {
            return parts;
        }

        @Override
        public void close() {
            for (Message part : parts) {
                if (part != null)
                    part.close();
            }
        }
    }

    private void sendInternal(String serviceName, Message[] parts, SendFlag flags,
                              boolean move) {
        if (parts == null || parts.length == 0)
            throw new IllegalArgumentException("parts required");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment vec = arena.allocate(NativeLayouts.MSG_LAYOUT, parts.length);
            int initialized = 0;
            try {
                for (int i = 0; i < parts.length; i++) {
                    Message part = parts[i];
                    if (part == null)
                        throw new IllegalArgumentException("parts[" + i + "] is null");
                    MemorySegment dest = vec.asSlice((long) i * MSG_SIZE, MSG_SIZE);
                    int rc = NativeMsg.msgInit(dest);
                    if (rc != 0)
                        throw new RuntimeException("zlink_msg_init failed");
                    initialized++;
                    if (move)
                        part.moveTo(dest);
                    else
                        part.copyTo(dest);
                }
                int rc = Native.gatewaySend(handle,
                    NativeHelpers.toCString(arena, serviceName), vec, parts.length,
                    flags.getValue());
                if (rc != 0)
                    throw new RuntimeException("zlink_gateway_send failed");
            } catch (RuntimeException ex) {
                closeMsgVector(vec, initialized);
                throw ex;
            }
        }
    }

    private static void closeMsgVector(MemorySegment vec, int count) {
        for (int i = 0; i < count; i++) {
            MemorySegment msg = vec.asSlice((long) i * MSG_SIZE, MSG_SIZE);
            NativeMsg.msgClose(msg);
        }
    }
}
