/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.util.Objects;

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
        Objects.requireNonNull(serviceName, "serviceName");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment service = NativeHelpers.toCString(arena, serviceName);
            sendInternal(arena, service, parts, flags, false);
        }
    }

    public void send(PreparedService service, Message[] parts, SendFlag flags) {
        Objects.requireNonNull(service, "service");
        try (Arena arena = Arena.ofConfined()) {
            sendInternal(arena, service.cString(), parts, flags, false);
        }
    }

    public void send(PreparedService service, Message[] parts, SendFlag flags,
                     SendContext context) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(context, "context");
        sendInternal(context, service.cString(), parts, flags, false);
    }

    public void send(PreparedService service, Message part, SendFlag flags,
                     SendContext context) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(part, "part");
        Objects.requireNonNull(context, "context");
        sendSingleInternal(context, service.cString(), part, flags, false);
    }

    public void sendMove(String serviceName, Message[] parts, SendFlag flags) {
        Objects.requireNonNull(serviceName, "serviceName");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment service = NativeHelpers.toCString(arena, serviceName);
            sendInternal(arena, service, parts, flags, true);
        }
    }

    public void sendConst(String serviceName, MemorySegment payload,
                          SendFlag flags) {
        sendConst(serviceName, payload, 0, payload.byteSize(), flags);
    }

    public void sendConst(String serviceName, MemorySegment payload, long offset,
                          long length, SendFlag flags) {
        Objects.requireNonNull(serviceName, "serviceName");
        Objects.requireNonNull(payload, "payload");
        validatePayloadRange(payload, offset, length);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment service = NativeHelpers.toCString(arena, serviceName);
            sendConstInternal(arena, service, payload, offset, length, flags);
        }
    }

    public void sendMove(PreparedService service, Message[] parts, SendFlag flags) {
        Objects.requireNonNull(service, "service");
        try (Arena arena = Arena.ofConfined()) {
            sendInternal(arena, service.cString(), parts, flags, true);
        }
    }

    public void sendMove(PreparedService service, Message[] parts,
                         SendFlag flags, SendContext context) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(context, "context");
        sendInternal(context, service.cString(), parts, flags, true);
    }

    public void sendMove(PreparedService service, Message part,
                         SendFlag flags, SendContext context) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(part, "part");
        Objects.requireNonNull(context, "context");
        sendSingleInternal(context, service.cString(), part, flags, true);
    }

    public void sendConst(PreparedService service, MemorySegment payload,
                          SendFlag flags) {
        sendConst(service, payload, 0, payload.byteSize(), flags);
    }

    public void sendConst(PreparedService service, MemorySegment payload,
                          long offset, long length, SendFlag flags) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(payload, "payload");
        validatePayloadRange(payload, offset, length);
        try (Arena arena = Arena.ofConfined()) {
            sendConstInternal(arena, service.cString(), payload, offset, length,
                flags);
        }
    }

    public void sendConst(PreparedService service, MemorySegment payload,
                          SendFlag flags, SendContext context) {
        sendConst(service, payload, 0, payload.byteSize(), flags, context);
    }

    public void sendConst(PreparedService service, MemorySegment payload,
                          long offset, long length, SendFlag flags,
                          SendContext context) {
        Objects.requireNonNull(service, "service");
        Objects.requireNonNull(payload, "payload");
        Objects.requireNonNull(context, "context");
        validatePayloadRange(payload, offset, length);
        sendConstInternal(context, service.cString(), payload, offset, length,
            flags);
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

    public RecvContext createRecvContext() {
        return new RecvContext();
    }

    public GatewayRawMessage recvRaw(ReceiveFlag flags, RecvContext context) {
        Objects.requireNonNull(context, "context");
        context.ensureOpen();
        int serviceLen = recvRawIntoContext(flags, context);
        MemorySegment serviceRaw = context.serviceName().asSlice(0, serviceLen);
        return new GatewayRawMessage(serviceRaw, context.reusableParts());
    }

    public GatewayRawBorrowed recvRawBorrowed(ReceiveFlag flags,
                                              RecvContext context) {
        Objects.requireNonNull(context, "context");
        context.ensureOpen();
        recvRawIntoContext(flags, context);
        GatewayRawBorrowed out = context.borrowedRaw();
        out.update(context.serviceName(), context.serviceNameLength(),
            context.reusableParts());
        return out;
    }

    public void setLoadBalancing(String serviceName, GatewayLbStrategy strategy) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.gatewaySetLbStrategy(handle, NativeHelpers.toCString(arena, serviceName), strategy.getValue());
            if (rc != 0)
                throw new RuntimeException("zlink_gateway_set_lb_strategy failed");
        }
    }

    public void setLoadBalancing(PreparedService service, GatewayLbStrategy strategy) {
        Objects.requireNonNull(service, "service");
        int rc = Native.gatewaySetLbStrategy(handle, service.cString(), strategy.getValue());
        if (rc != 0)
            throw new RuntimeException("zlink_gateway_set_lb_strategy failed");
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

    public int connectionCount(PreparedService service) {
        Objects.requireNonNull(service, "service");
        int rc = Native.gatewayConnectionCount(handle, service.cString());
        if (rc < 0)
            throw new RuntimeException("zlink_gateway_connection_count failed");
        return rc;
    }

    public PreparedService prepareService(String serviceName) {
        return new PreparedService(serviceName);
    }

    public SendContext createSendContext() {
        return new SendContext();
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

    public record GatewayRawMessage(MemorySegment serviceName, Message[] parts) {}

    public static final class GatewayRawBorrowed {
        private MemorySegment serviceNameBuffer = MemorySegment.NULL;
        private int serviceNameLength = 0;
        private Message[] parts = new Message[0];

        private GatewayRawBorrowed() {
        }

        public MemorySegment serviceName() {
            if (serviceNameBuffer.address() == 0 || serviceNameLength <= 0)
                return MemorySegment.NULL;
            return serviceNameBuffer.asSlice(0, serviceNameLength);
        }

        public MemorySegment serviceNameBuffer() {
            return serviceNameBuffer;
        }

        public int serviceNameLength() {
            return serviceNameLength;
        }

        public Message[] parts() {
            return parts;
        }

        void update(MemorySegment serviceNameBuffer,
                    int serviceNameLength,
                    Message[] parts) {
            this.serviceNameBuffer = serviceNameBuffer == null
                ? MemorySegment.NULL : serviceNameBuffer;
            this.serviceNameLength = Math.max(serviceNameLength, 0);
            this.parts = parts == null ? new Message[0] : parts;
        }
    }

    public static final class PreparedService implements AutoCloseable {
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

    public static final class SendContext implements AutoCloseable {
        private Arena arena;
        private MemorySegment vec;
        private int vecCapacity;
        private MemorySegment constTemplate;
        private boolean constTemplateInitialized;
        private MemorySegment constPayload;
        private long constOffset;
        private long constLength;

        SendContext() {
            this.arena = Arena.ofConfined();
            this.vec = MemorySegment.NULL;
            this.vecCapacity = 0;
            this.constTemplate = MemorySegment.NULL;
            this.constTemplateInitialized = false;
            this.constPayload = MemorySegment.NULL;
            this.constOffset = -1;
            this.constLength = -1;
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

        MemorySegment ensureConstTemplate(MemorySegment payload,
                                          long offset,
                                          long length) {
            ensureOpen();
            if (constTemplate.address() == 0) {
                constTemplate = arena.allocate(NativeLayouts.MSG_LAYOUT);
            }

            if (constTemplateInitialized
                && constPayload == payload
                && constOffset == offset
                && constLength == length) {
                return constTemplate;
            }

            if (constTemplateInitialized) {
                NativeMsg.msgClose(constTemplate);
                constTemplateInitialized = false;
            }

            MemorySegment slice = length == 0 ? MemorySegment.NULL
                : payload.asSlice(offset, length);
            int rc = NativeMsg.msgInitData(constTemplate, slice, length,
                MemorySegment.NULL, MemorySegment.NULL);
            if (rc != 0)
                throw new RuntimeException("zlink_msg_init_data failed");
            constTemplateInitialized = true;
            constPayload = payload;
            constOffset = offset;
            constLength = length;
            return constTemplate;
        }

        @Override
        public void close() {
            if (constTemplateInitialized) {
                NativeMsg.msgClose(constTemplate);
                constTemplateInitialized = false;
            }
            if (arena != null && arena.scope().isAlive())
                arena.close();
            arena = null;
            vec = MemorySegment.NULL;
            vecCapacity = 0;
            constTemplate = MemorySegment.NULL;
            constPayload = MemorySegment.NULL;
            constOffset = -1;
            constLength = -1;
        }
    }

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
            Message.closeAll(parts);
        }
    }

    public static final class RecvContext implements AutoCloseable {
        private Arena arena;
        private final MemorySegment partsPtr;
        private final MemorySegment partCount;
        private final MemorySegment serviceName;
        private int serviceNameLength;
        private Message[] reusableParts;
        private final GatewayRawBorrowed borrowedRaw;

        RecvContext() {
            this.arena = Arena.ofShared();
            this.partsPtr = arena.allocate(ValueLayout.ADDRESS);
            this.partCount = arena.allocate(ValueLayout.JAVA_LONG);
            this.serviceName = arena.allocate(SERVICE_NAME_CAPACITY);
            this.serviceNameLength = 0;
            this.reusableParts = new Message[0];
            this.borrowedRaw = new GatewayRawBorrowed();
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

        int serviceNameLength() {
            ensureOpen();
            return serviceNameLength;
        }

        Message[] reusableParts() {
            ensureOpen();
            return reusableParts;
        }

        void setServiceNameLength(int length) {
            ensureOpen();
            serviceNameLength = Math.max(length, 0);
        }

        void setReusableParts(Message[] parts) {
            ensureOpen();
            reusableParts = parts == null ? new Message[0] : parts;
        }

        GatewayRawBorrowed borrowedRaw() {
            ensureOpen();
            return borrowedRaw;
        }

        @Override
        public void close() {
            Message.closeAll(reusableParts);
            reusableParts = new Message[0];
            serviceNameLength = 0;
            borrowedRaw.update(MemorySegment.NULL, 0, reusableParts);
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
                    throw new RuntimeException("zlink_msg_init failed");
                initialized++;
                if (move)
                    part.moveTo(dest);
                else
                    part.copyTo(dest);
            }
            int rc = Native.gatewaySend(handle, serviceName, vec, parts.length,
                flags.getValue());
            if (rc != 0)
                throw new RuntimeException("zlink_gateway_send failed");
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
                throw new RuntimeException("zlink_msg_init failed");
            initialized = 1;
            if (move)
                part.moveTo(vec);
            else
                part.copyTo(vec);
            rc = Native.gatewaySend(handle, serviceName, vec, 1,
                flags.getValue());
            if (rc != 0)
                throw new RuntimeException("zlink_gateway_send failed");
        } catch (RuntimeException ex) {
            closeMsgVector(vec, initialized);
            throw ex;
        }
    }

    private void sendConstInternal(Arena arena,
                                   MemorySegment serviceName,
                                   MemorySegment payload,
                                   long offset,
                                   long length,
                                   SendFlag flags) {
        MemorySegment vec = arena.allocate(NativeLayouts.MSG_LAYOUT, 1);
        sendConstInternal(vec, serviceName, payload, offset, length, flags);
    }

    private void sendConstInternal(SendContext context,
                                   MemorySegment serviceName,
                                   MemorySegment payload,
                                   long offset,
                                   long length,
                                   SendFlag flags) {
        MemorySegment vec = context.ensureVector(1);
        int initialized = 0;
        try {
            MemorySegment template = context.ensureConstTemplate(payload, offset,
                length);
            int rc = NativeMsg.msgInit(vec);
            if (rc != 0)
                throw new RuntimeException("zlink_msg_init failed");
            rc = NativeMsg.msgCopy(vec, template);
            if (rc != 0)
                throw new RuntimeException("zlink_msg_copy failed");
            initialized = 1;
            rc = Native.gatewaySend(handle, serviceName, vec, 1,
                flags.getValue());
            if (rc != 0)
                throw new RuntimeException("zlink_gateway_send failed");
        } catch (RuntimeException ex) {
            closeMsgVector(vec, initialized);
            throw ex;
        }
    }

    private void sendConstInternal(MemorySegment vec,
                                   MemorySegment serviceName,
                                   MemorySegment payload,
                                   long offset,
                                   long length,
                                   SendFlag flags) {
        int initialized = 0;
        try {
            MemorySegment slice = length == 0 ? MemorySegment.NULL
                : payload.asSlice(offset, length);
            int rc = NativeMsg.msgInitData(vec, slice, length,
                MemorySegment.NULL, MemorySegment.NULL);
            if (rc != 0)
                throw new RuntimeException("zlink_msg_init_data failed");
            initialized = 1;
            rc = Native.gatewaySend(handle, serviceName, vec, 1,
                flags.getValue());
            if (rc != 0)
                throw new RuntimeException("zlink_gateway_send failed");
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

    private int recvRawIntoContext(ReceiveFlag flags,
                                   RecvContext context) {
        int rc = Native.gatewayRecv(handle,
            context.partsPtr(),
            context.partCount(),
            flags.getValue(),
            context.serviceName());
        if (rc != 0)
            throw new RuntimeException("zlink_gateway_recv failed");
        long partCount = context.partCount().get(ValueLayout.JAVA_LONG, 0);
        MemorySegment partsAddr = context.partsPtr().get(ValueLayout.ADDRESS, 0);
        Message[] reusable = Message.fromMsgVector(partsAddr, partCount,
            context.reusableParts());
        context.setReusableParts(reusable);
        int serviceLen = NativeHelpers.cStringLength(context.serviceName(),
            SERVICE_NAME_CAPACITY);
        context.setServiceNameLength(serviceLen);
        return serviceLen;
    }

    private static void validatePayloadRange(MemorySegment payload,
                                             long offset,
                                             long length) {
        if (!payload.isNative())
            throw new IllegalArgumentException(
                "sendConst requires a native MemorySegment");
        long total = payload.byteSize();
        if (offset < 0 || length < 0 || offset > total - length)
            throw new IndexOutOfBoundsException("payload range out of bounds");
    }

}
