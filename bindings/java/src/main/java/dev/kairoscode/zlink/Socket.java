/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import dev.kairoscode.zlink.options.SocketOptionKey;
import dev.kairoscode.zlink.options.SocketOptions;
import dev.kairoscode.zlink.options.SocketOptionValueType;
import dev.kairoscode.zlink.service.discovery.Discovery;
import io.netty.buffer.ByteBuf;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Objects;

/**
 * Canonical raw socket surface for zlink.
 *
 * <p>{@code Socket} owns transport behavior such as bind/connect, raw
 * multipart send/recv, topic publish/subscribe helpers, callback attachment,
 * and discovery attach. Payload conversion stays on {@link Message}; recv
 * results are surfaced as {@link Received}.
 */
public final class Socket implements AutoCloseable {
    private static final int DEFAULT_IO_BUFFER_SIZE = 8192;
    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;
    private static final int STREAM_ROUTING_ID_SIZE = 4;
    private static final int STREAM_ROUTING_ID_LAYOUT_SIZE = 256;
    private static final int TOPIC_CAPACITY = 256;
    private static final long STREAM_ROUTING_ID_MAX = 0xFFFF_FFFFL;
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_STREAM_CALLBACK =
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_STREAM_CALLBACK_RAW =
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_RECV_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_SUBSCRIBE_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
        ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_SEND_READY_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS);
    private static final byte[] EMPTY_BYTES = new byte[0];

    private MemorySegment handle;
    private final boolean own;
    private final SocketType socketTypeHint;
    private Arena sendScratchArena = Arena.ofShared();
    private Arena recvScratchArena = Arena.ofShared();
    private MemorySegment sendScratch = MemorySegment.NULL;
    private int sendScratchCapacity = DEFAULT_IO_BUFFER_SIZE;
    private MemorySegment recvScratch = MemorySegment.NULL;
    private int recvScratchCapacity = DEFAULT_IO_BUFFER_SIZE;
    private StreamPacketHandler streamRawHandler;
    private StreamPacketBatchHandler streamBatchHandler;
    private Arena streamCallbackArena;
    private MemorySegment streamCallbackStub = MemorySegment.NULL;
    private boolean streamAttached;
    private final ThreadLocal<MemorySegment> streamRoutingIdScratch =
      ThreadLocal.withInitial(
        () -> Arena.ofAuto().allocate(STREAM_ROUTING_ID_LAYOUT_SIZE));
    private final ThreadLocal<Message> recvFrameScratch =
      ThreadLocal.withInitial(Message::new);
    private final ThreadLocal<LegacyReceiveState> legacyReceiveState =
      ThreadLocal.withInitial(LegacyReceiveState::new);
    private SocketMessageHandler receiveHandler;
    private SubscribeHandler subscribeHandler;
    private SendReadyHandler sendReadyHandler;
    private Arena receiveCallbackArena;
    private Arena subscribeCallbackArena;
    private Arena sendReadyCallbackArena;
    private MemorySegment receiveCallbackStub = MemorySegment.NULL;
    private MemorySegment subscribeCallbackStub = MemorySegment.NULL;
    private MemorySegment sendReadyCallbackStub = MemorySegment.NULL;
    private volatile RuntimeException callbackFailure;

    private static final class LegacyReceiveState {
        private Message[] frames = new Message[0];
        private int index;

        boolean hasPending() {
            return index < frames.length;
        }

        int pendingCount() {
            return frames.length - index;
        }

        Message poll() {
            Message frame = frames[index++];
            frame.setMore(index < frames.length);
            if (!hasPending()) {
                frames = new Message[0];
                index = 0;
            }
            return frame;
        }

        void replace(Message[] nextFrames) {
            closeRemaining();
            frames = nextFrames;
            index = 0;
        }

        void closeRemaining() {
            for (int i = index; i < frames.length; i++) {
                if (frames[i] != null) {
                    try {
                        frames[i].close();
                    } catch (RuntimeException ignored) {
                    }
                }
            }
            frames = new Message[0];
            index = 0;
        }
    }

    public Socket(Context ctx, SocketType type) {
        this.handle = Native.socket(ctx.handle(), type.getValue());
        if (handle == null || handle.address() == 0)
            throw ZlinkException.fromLastError("zlink_socket");
        this.own = true;
        this.socketTypeHint = type;
    }

    private Socket(MemorySegment handle, boolean own, SocketType socketTypeHint) {
        this.handle = handle;
        this.own = own;
        this.socketTypeHint = socketTypeHint;
    }

    public static Socket adopt(MemorySegment handle, boolean own) {
        if (handle == null || handle.address() == 0)
            throw new IllegalArgumentException("invalid socket handle");
        return new Socket(handle, own, null);
    }

    /** Binds the socket to the endpoint. */
    public void bind(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment addr = arena.allocateFrom(endpoint, StandardCharsets.UTF_8);
            int rc = Native.bind(handle, addr);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_bind");
        }
    }

    /** Connects the socket to the endpoint. */
    public void connect(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment addr = arena.allocateFrom(endpoint, StandardCharsets.UTF_8);
            int rc = Native.connect(handle, addr);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_connect");
        }
    }

    /** Unbinds the socket from the endpoint. */
    public void unbind(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment addr = arena.allocateFrom(endpoint, StandardCharsets.UTF_8);
            int rc = Native.unbind(handle, addr);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_unbind");
        }
    }

    /** Disconnects the socket from the endpoint. */
    public void disconnect(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment addr = arena.allocateFrom(endpoint, StandardCharsets.UTF_8);
            int rc = Native.disconnect(handle, addr);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_disconnect");
        }
    }

    /** Attaches a fixed-service discovery view to the socket. */
    public void attachDiscovery(Discovery discovery) {
        Objects.requireNonNull(discovery, "discovery");
        int rc = Native.socketAttachDiscovery(handle, discovery.handle());
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_socket_attach_discovery");
    }

    public void setSockOpt(SocketOption option, byte[] value) {
        setSockOpt(option, value, 0, value.length);
    }

    public void setSockOpt(SocketOption option, byte[] value, int offset, int length) {
        Objects.requireNonNull(value, "value");
        validateRange(value.length, offset, length, "value");
        setSockOptBytes(option.getValue(), value, offset, length);
    }

    public void setSockOpt(SocketOption option, ByteBuffer value) {
        Objects.requireNonNull(value, "value");
        int length = value.remaining();
        if (length == 0) {
            setSockOptRaw(option.getValue(), MemorySegment.NULL, 0);
            return;
        }
        MemorySegment srcSeg = MemorySegment.ofBuffer(value);
        MemorySegment seg;
        if (value.isDirect()) {
            seg = srcSeg;
        } else {
            seg = ensureSendScratch(length);
            MemorySegment.copy(srcSeg, 0, seg, 0, length);
        }
        setSockOptRaw(option.getValue(), seg, length);
        value.position(value.position() + length);
    }

    public void setSockOpt(SocketOption option, int value) {
        setSockOptInt(option.getValue(), value);
    }

    public byte[] getSockOptBytes(SocketOption option, int maxLen) {
        return getSockOptBytes(option.getValue(), maxLen);
    }

    public int getSockOptInt(SocketOption option) {
        return getSockOptInt(option.getValue());
    }

    public void setOption(SocketOptionKey<Integer> option, int value) {
        Objects.requireNonNull(option, "option");
        validateAmbiguousOption(option);
        validateOptionType(option, SocketOptionValueType.INT32);
        option.requireWritable();
        setSockOptInt(option.optionId(), value);
    }

    public void setOption(SocketOptionKey<Long> option, long value) {
        Objects.requireNonNull(option, "option");
        validateAmbiguousOption(option);
        validateOptionType(option, SocketOptionValueType.INT64);
        option.requireWritable();
        setSockOptLong(option.optionId(), value);
    }

    public void setOption(SocketOptionKey<String> option, String value) {
        Objects.requireNonNull(option, "option");
        validateAmbiguousOption(option);
        validateOptionType(option, SocketOptionValueType.STRING);
        option.requireWritable();
        byte[] utf8 = Objects.requireNonNull(value, "value").getBytes(
          StandardCharsets.UTF_8);
        setTypedBytesOption(option.optionId(), utf8, 0, utf8.length);
    }

    public void setOption(SocketOptionKey<byte[]> option, byte[] value) {
        Objects.requireNonNull(option, "option");
        validateAmbiguousOption(option);
        validateOptionType(option, SocketOptionValueType.BYTES);
        option.requireWritable();
        Objects.requireNonNull(value, "value");
        setTypedBytesOption(option.optionId(), value, 0, value.length);
    }

    @SuppressWarnings("unchecked")
    public <T> T getOption(SocketOptionKey<T> option) {
        Objects.requireNonNull(option, "option");
        validateAmbiguousOption(option);
        option.requireReadable();
        return switch (option.valueType()) {
            case INT32 -> (T) Integer.valueOf(getSockOptInt(option.optionId()));
            case INT64 -> (T) Long.valueOf(getSockOptLong(option.optionId()));
            case STRING -> (T) getTypedStringOption(option);
            case BYTES -> (T) getTypedBytesOption(option);
        };
    }

    public MonitorSocket monitorOpen(int events) {
        MemorySegment sock = Native.monitorOpen(handle, events);
        if (sock == null || sock.address() == 0)
            throw ZlinkException.fromLastError("zlink_socket_monitor_open");
        return new MonitorSocket(Socket.adopt(sock, true));
    }

    public void send(Message part) {
        send(part, SendFlag.NONE);
    }

    public void send(Message part, SendFlag flags) {
        Objects.requireNonNull(part, "part");
        Objects.requireNonNull(flags, "flags");
        sendParts(null, List.of(part), flags, false);
    }

    public void send(List<Message> parts) {
        send(parts, SendFlag.NONE);
    }

    public void send(List<Message> parts, SendFlag flags) {
        Objects.requireNonNull(parts, "parts");
        Objects.requireNonNull(flags, "flags");
        sendParts(null, parts, flags, false);
    }

    public void send(RoutingId rid, Message part) {
        send(rid, part, SendFlag.NONE);
    }

    public void send(RoutingId rid, Message part, SendFlag flags) {
        Objects.requireNonNull(part, "part");
        Objects.requireNonNull(flags, "flags");
        sendParts(rid, List.of(part), flags, false);
    }

    public void send(RoutingId rid, List<Message> parts) {
        send(rid, parts, SendFlag.NONE);
    }

    public void send(RoutingId rid, List<Message> parts, SendFlag flags) {
        Objects.requireNonNull(parts, "parts");
        Objects.requireNonNull(flags, "flags");
        sendParts(rid, parts, flags, false);
    }

    /** Publishes a single payload part to a topic-aware socket. */
    public void publish(String topicId, Message part) {
        publish(topicId, part, SendFlag.NONE);
    }

    /** Publishes a single payload part with explicit send flags. */
    public void publish(String topicId, Message part, SendFlag flags) {
        Objects.requireNonNull(part, "part");
        publish(topicId, List.of(part), flags);
    }

    /** Publishes a multipart payload to a topic-aware socket. */
    public void publish(String topicId, List<Message> parts) {
        publish(topicId, parts, SendFlag.NONE);
    }

    /** Publishes a multipart payload with explicit send flags. */
    public void publish(String topicId, List<Message> parts, SendFlag flags) {
        Objects.requireNonNull(topicId, "topicId");
        Objects.requireNonNull(parts, "parts");
        Objects.requireNonNull(flags, "flags");
        publishParts(topicId, parts, flags, false);
    }

    public Received recv() {
        return recv(ReceiveFlag.NONE);
    }

    public Received recv(ReceiveFlag flags) {
        Objects.requireNonNull(flags, "flags");
        while (true) {
            Native.MultipartReceive received = Native.recvMultipart(handle,
                flags.getValue());
            if (received != null) {
                RoutingId rid = toRoutingId(received.routingId());
                Message[] parts = Message.fromMsgVector(received.parts(),
                    received.partCount());
                return new Received(rid, parts);
            }

            int errno = Native.errno();
            if (errno == ERRNO_EINTR)
                continue;
            throw ZlinkException.fromLastError("zlink_recv");
        }
    }

    /** Receives a topic-aware delivery from a SUB/XSUB-style socket. */
    public TopicMessage subscribe() {
        return subscribe(ReceiveFlag.NONE);
    }

    /** Receives a topic-aware delivery with explicit receive flags. */
    public TopicMessage subscribe(ReceiveFlag flags) {
        Objects.requireNonNull(flags, "flags");
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
            rid.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
                (byte) 0);
            MemorySegment partsOut = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment partCountOut = arena.allocate(ValueLayout.JAVA_LONG);
            MemorySegment topicOut = arena.allocate(TOPIC_CAPACITY);
            MemorySegment topicLenOut = arena.allocate(ValueLayout.JAVA_LONG);
            topicLenOut.set(ValueLayout.JAVA_LONG, 0, TOPIC_CAPACITY);

            int rc = Native.subscribe(handle, rid, partsOut, partCountOut,
                topicOut, topicLenOut, flags.getValue());
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_subscribe");

            byte[] routingId = decodeRoutingId(rid);
            long partCount = partCountOut.get(ValueLayout.JAVA_LONG, 0);
            MemorySegment partsAddr = partsOut.get(ValueLayout.ADDRESS, 0);
            Message[] parts = Message.fromMsgVector(partsAddr, partCount);
            int topicLength = normalizeTopicLength(topicOut, TOPIC_CAPACITY,
                topicLenOut.get(ValueLayout.JAVA_LONG, 0));
            String topicId = topicLength == 0
                ? ""
                : new String(topicOut.asSlice(0, topicLength).toArray(
                    ValueLayout.JAVA_BYTE), StandardCharsets.UTF_8);
            return new TopicMessage(toRoutingId(routingId), topicId, parts);
        }
    }

    public void setRoutingId(RoutingId rid) {
        Objects.requireNonNull(rid, "rid");
        byte[] value = rid.toByteArray();
        setRoutingIdBytes(value, 0, value.length);
    }

    public RoutingId routingId() {
        return RoutingId.copyOf(getRoutingIdBytes());
    }

    public void setSubscription(String filter) {
        Objects.requireNonNull(filter, "filter");
        byte[] bytes = filter.getBytes(StandardCharsets.UTF_8);
        setSubscriptionBytes(bytes, 0, bytes.length, true);
    }

    public void setSubscription(byte[] filter) {
        Objects.requireNonNull(filter, "filter");
        setSubscriptionBytes(filter, 0, filter.length, true);
    }

    public void unsetSubscription(String filter) {
        Objects.requireNonNull(filter, "filter");
        byte[] bytes = filter.getBytes(StandardCharsets.UTF_8);
        setSubscriptionBytes(bytes, 0, bytes.length, false);
    }

    public void unsetSubscription(byte[] filter) {
        Objects.requireNonNull(filter, "filter");
        setSubscriptionBytes(filter, 0, filter.length, false);
    }

    public List<SubscriptionEntry> subscriptions() {
        ensureOpen();
        ArrayList<SubscriptionEntry> out = new ArrayList<>();
        int capacity = 64;
        try (Arena arena = Arena.ofConfined()) {
            for (long index = 0;; index++) {
                MemorySegment lenInOut = arena.allocate(ValueLayout.JAVA_LONG);
                MemorySegment isPatternOut = arena.allocate(ValueLayout.JAVA_INT);
                byte[] filter = subscriptionAt(index, lenInOut, isPatternOut,
                    capacity);
                if (filter == null)
                    break;
                capacity = Math.max(capacity, filter.length);
                out.add(new SubscriptionEntry(filter,
                    isPatternOut.get(ValueLayout.JAVA_INT, 0) != 0));
            }
        }
        return List.copyOf(out);
    }

    public void onReceive(SocketMessageHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        ensureNoCallbackFailure();
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(callbackHandle("handleReceiveCallback",
            MethodType.methodType(void.class, MemorySegment.class,
                MemorySegment.class, long.class, MemorySegment.class)),
            FD_RECV_CALLBACK, arena);
        boolean success = false;
        try {
            int rc = Native.recvHandler(handle, stub, MemorySegment.NULL);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_recv_handler");
            success = true;
            closeArena(receiveCallbackArena);
            receiveCallbackArena = arena;
            receiveCallbackStub = stub;
            receiveHandler = handler;
        } finally {
            if (!success)
                closeArena(arena);
        }
    }

    public void onSubscribe(SubscribeHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        ensureNoCallbackFailure();
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(callbackHandle(
            "handleSubscribeCallback", MethodType.methodType(void.class,
                MemorySegment.class, MemorySegment.class, long.class,
                MemorySegment.class, long.class, MemorySegment.class)),
            FD_SUBSCRIBE_CALLBACK, arena);
        boolean success = false;
        try {
            int rc = Native.subscribeHandler(handle, stub, MemorySegment.NULL);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_subscribe_handler");
            success = true;
            closeArena(subscribeCallbackArena);
            subscribeCallbackArena = arena;
            subscribeCallbackStub = stub;
            subscribeHandler = handler;
        } finally {
            if (!success)
                closeArena(arena);
        }
    }

    public void onSendReady(SendReadyHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        ensureNoCallbackFailure();
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(callbackHandle(
            "handleSendReadyCallback", MethodType.methodType(void.class,
                MemorySegment.class, MemorySegment.class)),
            FD_SEND_READY_CALLBACK, arena);
        boolean success = false;
        try {
            int rc = Native.sendReadyHandler(handle, stub, MemorySegment.NULL);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_send_ready_handler");
            success = true;
            closeArena(sendReadyCallbackArena);
            sendReadyCallbackArena = arena;
            sendReadyCallbackStub = stub;
            sendReadyHandler = handler;
        } finally {
            if (!success)
                closeArena(arena);
        }
    }

    @Deprecated(forRemoval = false)
    int send(byte[] data, SendFlag flag) {
        Objects.requireNonNull(flag, "flag");
        return send(data, 0, data.length, flag.getValue());
    }

    @Deprecated(forRemoval = false)
    boolean trySend(byte[] data, SendFlag flag) {
        Objects.requireNonNull(flag, "flag");
        return trySend(data, 0, data.length, flag.getValue());
    }

    @Deprecated(forRemoval = false)
    boolean trySend(byte[] data, int offset, int length, SendFlag flag) {
        Objects.requireNonNull(flag, "flag");
        return trySend(data, offset, length, flag.getValue());
    }

    @Deprecated(forRemoval = false)
    int send(byte[] data, int offset, int length, SendFlag flag) {
        Objects.requireNonNull(flag, "flag");
        return send(data, offset, length, flag.getValue());
    }

    private int send(byte[] data, int offset, int length, int sendFlags) {
        Objects.requireNonNull(data, "data");
        validateRange(data.length, offset, length, "data");
        try (Message msg = Message.fromBytes(data, offset, length)) {
            sendMessageFrame(msg, SendFlag.fromValue(sendFlags));
            return length;
        }
    }

    private boolean trySend(byte[] data, int offset, int length, int sendFlags) {
        Objects.requireNonNull(data, "data");
        validateRange(data.length, offset, length, "data");
        try (Message msg = Message.fromBytes(data, offset, length)) {
            return trySendMessageFrame(msg, SendFlag.fromValue(sendFlags));
        }
    }

    @Deprecated(forRemoval = false)
    int send(ByteBuffer buffer, SendFlag flag) {
        Objects.requireNonNull(flag, "flag");
        return send(buffer, flag.getValue());
    }

    @Deprecated(forRemoval = false)
    boolean trySend(ByteBuffer buffer, SendFlag flag) {
        Objects.requireNonNull(flag, "flag");
        return trySend(buffer, flag.getValue());
    }

    private int send(ByteBuffer buffer, int sendFlags) {
        Objects.requireNonNull(buffer, "buffer");
        int length = buffer.remaining();
        ByteBuffer slice = buffer.slice();
        try (Message msg = buffer.isDirect()
            ? Message.fromDirectByteBuffer(slice)
            : Message.fromByteBuffer(slice)) {
            sendMessageFrame(msg, SendFlag.fromValue(sendFlags));
            buffer.position(buffer.position() + length);
            return length;
        }
    }

    private boolean trySend(ByteBuffer buffer, int sendFlags) {
        Objects.requireNonNull(buffer, "buffer");
        int length = buffer.remaining();
        ByteBuffer slice = buffer.slice();
        try (Message msg = buffer.isDirect()
            ? Message.fromDirectByteBuffer(slice)
            : Message.fromByteBuffer(slice)) {
            boolean sent = trySendMessageFrame(msg, SendFlag.fromValue(sendFlags));
            if (sent) {
                buffer.position(buffer.position() + length);
            }
            return sent;
        }
    }

    @Deprecated(forRemoval = false)
    int send(ByteSpan span, SendFlag flag) {
        Objects.requireNonNull(span, "span");
        Objects.requireNonNull(flag, "flag");
        return send(span.segment(), 0, span.length(), flag.getValue());
    }

    @Deprecated(forRemoval = false)
    int send(MemorySegment segment, SendFlag flag) {
        Objects.requireNonNull(segment, "segment");
        Objects.requireNonNull(flag, "flag");
        return send(segment, 0, segment.byteSize(), flag.getValue());
    }

    @Deprecated(forRemoval = false)
    boolean trySend(MemorySegment segment, SendFlag flag) {
        Objects.requireNonNull(segment, "segment");
        Objects.requireNonNull(flag, "flag");
        return trySend(segment, 0, segment.byteSize(), flag.getValue());
    }

    @Deprecated(forRemoval = false)
    boolean trySend(MemorySegment segment, long offset, long length,
                           SendFlag flag) {
        Objects.requireNonNull(flag, "flag");
        return trySend(segment, offset, length, flag.getValue());
    }

    @Deprecated(forRemoval = false)
    int send(MemorySegment segment, long offset, long length,
                    SendFlag flag) {
        Objects.requireNonNull(flag, "flag");
        return send(segment, offset, length, flag.getValue());
    }

    private int send(MemorySegment segment, long offset, long length,
                     int sendFlags) {
        Objects.requireNonNull(segment, "segment");
        validateRange(segment.byteSize(), offset, length, "segment");
        try (Message msg = segment.isNative()
            ? Message.fromNativeData(segment, offset, length)
            : Message.fromMemorySegment(segment, offset, length)) {
            sendMessageFrame(msg, SendFlag.fromValue(sendFlags));
            return (int) length;
        }
    }

    private boolean trySend(MemorySegment segment, long offset, long length,
                            int sendFlags) {
        Objects.requireNonNull(segment, "segment");
        validateRange(segment.byteSize(), offset, length, "segment");
        try (Message msg = segment.isNative()
            ? Message.fromNativeData(segment, offset, length)
            : Message.fromMemorySegment(segment, offset, length)) {
            return trySendMessageFrame(msg, SendFlag.fromValue(sendFlags));
        }
    }

    @Deprecated(forRemoval = false)
    int send(ByteBuf buf, SendFlag flag) {
        Objects.requireNonNull(buf, "buf");
        Objects.requireNonNull(flag, "flag");
        return send(buf, flag.getValue());
    }

    @Deprecated(forRemoval = false)
    boolean trySend(ByteBuf buf, SendFlag flag) {
        Objects.requireNonNull(buf, "buf");
        Objects.requireNonNull(flag, "flag");
        return trySend(buf, flag.getValue());
    }

    private int send(ByteBuf buf, int sendFlags) {
        Objects.requireNonNull(buf, "buf");
        int len = buf.readableBytes();
        if (len <= 0) {
            try (Message msg = Message.fromBytes(EMPTY_BYTES)) {
                sendMessageFrame(msg, SendFlag.fromValue(sendFlags));
                return 0;
            }
        }

        int readerIndex = buf.readerIndex();
        MemorySegment directSeg = nettyReadableSegment(buf, readerIndex, len);
        if (directSeg.address() != 0) {
            int rc = send(directSeg, 0, len, sendFlags);
            if (rc > 0) {
                buf.readerIndex(readerIndex + rc);
            }
            return rc;
        }
        try {
            ByteBuffer nio = nettyReadableBuffer(buf, readerIndex, len);
            int rc = send(nio, sendFlags);
            if (rc > 0)
                buf.readerIndex(readerIndex + rc);
            return rc;
        } catch (UnsupportedOperationException ex) {
            return sendNettyFallback(buf, readerIndex, len, sendFlags);
        }
    }

    private boolean trySend(ByteBuf buf, int sendFlags) {
        Objects.requireNonNull(buf, "buf");
        int len = buf.readableBytes();
        if (len <= 0) {
            try (Message msg = Message.fromBytes(EMPTY_BYTES)) {
                return trySendMessageFrame(msg, SendFlag.fromValue(sendFlags));
            }
        }

        int readerIndex = buf.readerIndex();
        MemorySegment directSeg = nettyReadableSegment(buf, readerIndex, len);
        if (directSeg.address() != 0) {
            boolean sent = trySend(directSeg, 0, len, sendFlags);
            if (sent) {
                buf.readerIndex(readerIndex + len);
            }
            return sent;
        }
        try {
            ByteBuffer nio = nettyReadableBuffer(buf, readerIndex, len);
            boolean sent = trySend(nio, sendFlags);
            if (sent) {
                buf.readerIndex(readerIndex + len);
            }
            return sent;
        } catch (UnsupportedOperationException ex) {
            return trySendNettyFallback(buf, readerIndex, len, sendFlags);
        }
    }

    private static UnsupportedOperationException unsupportedLegacySocketApi(
      String api) {
        return new UnsupportedOperationException(
          "Legacy socket API '" + api
            + "' is not part of the canonical Java binding surface.");
    }

    public void attachStream(StreamPacketHandler handler,
                             StreamDispatchMode mode) {
        throw unsupportedLegacySocketApi("attachStream(StreamPacketHandler, StreamDispatchMode)");
    }

    public void attachStream(StreamPacketBatchHandler handler,
                             StreamDispatchMode mode) {
        throw unsupportedLegacySocketApi("attachStream(StreamPacketBatchHandler, StreamDispatchMode)");
    }

    public void attachStream(StreamPacketHandler handler) {
        throw unsupportedLegacySocketApi("attachStream(StreamPacketHandler)");
    }

    public void attachStreamRaw(StreamPacketHandler handler) {
        throw unsupportedLegacySocketApi("attachStreamRaw");
    }

    public void attachStreamLen32be(StreamPacketBatchHandler handler) {
        throw unsupportedLegacySocketApi("attachStreamLen32be");
    }

    public void detachStream() {
        throw unsupportedLegacySocketApi("detachStream");
    }

    public byte[] streamPeerRoutingIdBytes(int index) {
        throw unsupportedLegacySocketApi("streamPeerRoutingIdBytes");
    }

    public byte[] streamPeerRoutingIdBytes() {
        return streamPeerRoutingIdBytes(0);
    }

    public Long streamPeerRoutingId(int index) {
        throw unsupportedLegacySocketApi("streamPeerRoutingId");
    }

    public Long streamPeerRoutingId() {
        return streamPeerRoutingId(0);
    }

    public int streamSend(long routingId, byte[] payload, SendFlag flags) {
        throw unsupportedLegacySocketApi("streamSend(long, byte[], SendFlag)");
    }

    public int streamSend(long routingId, byte[] payload) {
        return streamSend(routingId, payload, SendFlag.NONE);
    }

    public int streamSend(long routingId, MemorySegment payload,
                          SendFlag flags) {
        throw unsupportedLegacySocketApi("streamSend(long, MemorySegment, SendFlag)");
    }

    public int streamSend(long routingId, MemorySegment payload) {
        return streamSend(routingId, payload, SendFlag.NONE);
    }

    public int streamSend(long routingId, Message payload, SendFlag flags) {
        throw unsupportedLegacySocketApi("streamSend(long, Message, SendFlag)");
    }

    public int streamSend(long routingId, Message payload) {
        return streamSend(routingId, payload, SendFlag.NONE);
    }

    public int streamSend(byte[] routingId, byte[] payload, SendFlag flags) {
        throw unsupportedLegacySocketApi("streamSend(byte[], byte[], SendFlag)");
    }

    public int streamSend(byte[] routingId, byte[] payload) {
        return streamSend(routingId, payload, SendFlag.NONE);
    }

    public int streamSend(ByteBuffer routingId, ByteBuffer payload,
                          SendFlag flags) {
        throw unsupportedLegacySocketApi("streamSend(ByteBuffer, ByteBuffer, SendFlag)");
    }

    public int streamSend(ByteBuffer routingId, ByteBuffer payload) {
        return streamSend(routingId, payload, SendFlag.NONE);
    }

    public int streamSend(ByteSpan routingId, ByteSpan payload,
                          SendFlag flags) {
        throw unsupportedLegacySocketApi("streamSend(ByteSpan, ByteSpan, SendFlag)");
    }

    public int streamSend(ByteSpan routingId, ByteSpan payload) {
        return streamSend(routingId, payload, SendFlag.NONE);
    }

    public int streamSend(MemorySegment routingId, MemorySegment payload,
                          SendFlag flags) {
        throw unsupportedLegacySocketApi("streamSend(MemorySegment, MemorySegment, SendFlag)");
    }

    public int streamSend(MemorySegment routingId, MemorySegment payload) {
        return streamSend(routingId, payload, SendFlag.NONE);
    }

    public int streamSend(byte[] routingId, Message payload, SendFlag flags) {
        throw unsupportedLegacySocketApi("streamSend(byte[], Message, SendFlag)");
    }

    public int streamSend(byte[] routingId, Message payload) {
        return streamSend(routingId, payload, SendFlag.NONE);
    }

    @Deprecated(forRemoval = false)
    byte[] recv(int size, ReceiveFlag flags) {
        if (size < 0)
            throw new IllegalArgumentException("size must be >= 0");
        if (size == 0)
            return new byte[0];
        try (Message frame = takeRecvFrame(flags, false)) {
            int rc = Math.min(size, frame.size());
            byte[] out = new byte[rc];
            if (rc > 0) {
                MemorySegment.copy(frame.dataSegment(), 0,
                  MemorySegment.ofArray(out), 0, rc);
            }
            return out;
        }
    }

    @Deprecated(forRemoval = false)
    int recv(byte[] data, ReceiveFlag flags) {
        Objects.requireNonNull(data, "data");
        return recv(data, 0, data.length, flags);
    }

    @Deprecated(forRemoval = false)
    int tryRecv(byte[] data, ReceiveFlag flags) {
        Objects.requireNonNull(data, "data");
        return tryRecv(data, 0, data.length, flags);
    }

    @Deprecated(forRemoval = false)
    int tryRecv(byte[] data, int offset, int length, ReceiveFlag flags) {
        Objects.requireNonNull(data, "data");
        validateRange(data.length, offset, length, "data");
        if (length == 0)
            return 0;
        try (Message frame = takeRecvFrame(flags, true)) {
            if (frame == null)
                return -1;
            int rc = Math.min(length, frame.size());
            if (rc > 0) {
                MemorySegment.copy(frame.dataSegment(), 0,
                  MemorySegment.ofArray(data), offset, rc);
            }
            return rc;
        }
    }

    @Deprecated(forRemoval = false)
    int recv(byte[] data, int offset, int length, ReceiveFlag flags) {
        Objects.requireNonNull(data, "data");
        validateRange(data.length, offset, length, "data");
        if (length == 0)
            return 0;
        try (Message frame = takeRecvFrame(flags, false)) {
            int rc = Math.min(length, frame.size());
            if (rc > 0) {
                MemorySegment.copy(frame.dataSegment(), 0,
                  MemorySegment.ofArray(data), offset, rc);
            }
            return rc;
        }
    }

    @Deprecated(forRemoval = false)
    int recv(ByteBuffer buffer, ReceiveFlag flags) {
        Objects.requireNonNull(buffer, "buffer");
        int writable = buffer.remaining();
        if (writable <= 0)
            return 0;
        try (Message frame = takeRecvFrame(flags, false)) {
            int rc = Math.min(writable, frame.size());
            if (rc > 0) {
                ByteBuffer dst = buffer.slice();
                dst.limit(rc);
                MemorySegment.copy(frame.dataSegment(), 0,
                  MemorySegment.ofBuffer(dst), 0, rc);
            }
            buffer.position(buffer.position() + rc);
            return rc;
        }
    }

    @Deprecated(forRemoval = false)
    int tryRecv(ByteBuffer buffer, ReceiveFlag flags) {
        Objects.requireNonNull(buffer, "buffer");
        int writable = buffer.remaining();
        if (writable <= 0)
            return 0;
        try (Message frame = takeRecvFrame(flags, true)) {
            if (frame == null)
                return -1;
            int rc = Math.min(writable, frame.size());
            if (rc > 0) {
                ByteBuffer dst = buffer.slice();
                dst.limit(rc);
                MemorySegment.copy(frame.dataSegment(), 0,
                  MemorySegment.ofBuffer(dst), 0, rc);
            }
            buffer.position(buffer.position() + rc);
            return rc;
        }
    }

    @Deprecated(forRemoval = false)
    int recv(ByteSpan span, ReceiveFlag flags) {
        Objects.requireNonNull(span, "span");
        return recv(span.segment(), 0, span.length(), flags);
    }

    @Deprecated(forRemoval = false)
    int recv(MemorySegment segment, ReceiveFlag flags) {
        Objects.requireNonNull(segment, "segment");
        return recv(segment, 0, segment.byteSize(), flags);
    }

    @Deprecated(forRemoval = false)
    int tryRecv(MemorySegment segment, ReceiveFlag flags) {
        Objects.requireNonNull(segment, "segment");
        return tryRecv(segment, 0, segment.byteSize(), flags);
    }

    @Deprecated(forRemoval = false)
    boolean recvFrameHasMore(ReceiveFlag flags) {
        Objects.requireNonNull(flags, "flags");

        Message msg = recvFrameScratch.get();
        msg.resetForReuse();
        recvMessageFrame(msg, flags);
        return msg.more();
    }

    @Deprecated(forRemoval = false)
    int recv(MemorySegment segment, long offset, long length, ReceiveFlag flags) {
        Objects.requireNonNull(segment, "segment");
        validateRange(segment.byteSize(), offset, length, "segment");
        if (length == 0)
            return 0;
        try (Message frame = takeRecvFrame(flags, false)) {
            int rc = Math.min(toIntLength(length), frame.size());
            if (rc > 0) {
                MemorySegment.copy(frame.dataSegment(), 0, segment, offset, rc);
            }
            return rc;
        }
    }

    @Deprecated(forRemoval = false)
    int tryRecv(MemorySegment segment, long offset, long length,
                       ReceiveFlag flags) {
        Objects.requireNonNull(segment, "segment");
        validateRange(segment.byteSize(), offset, length, "segment");
        if (length == 0)
            return 0;
        try (Message frame = takeRecvFrame(flags, true)) {
            if (frame == null)
                return -1;
            int rc = Math.min(toIntLength(length), frame.size());
            if (rc > 0) {
                MemorySegment.copy(frame.dataSegment(), 0, segment, offset, rc);
            }
            return rc;
        }
    }

    @Deprecated(forRemoval = false)
    int recv(ByteBuf buf, ReceiveFlag flags) {
        Objects.requireNonNull(buf, "buf");
        Objects.requireNonNull(flags, "flags");
        int writable = buf.writableBytes();
        if (writable <= 0)
            return 0;

        int writerIndex = buf.writerIndex();
        MemorySegment directSeg = nettyWritableSegment(buf, writerIndex, writable);
        if (directSeg.address() != 0) {
            int rc = recv(directSeg, 0, writable, flags);
            if (rc > 0)
                buf.writerIndex(writerIndex + rc);
            return rc;
        }
        try {
            ByteBuffer nio = nettyWritableBuffer(buf, writerIndex, writable);
            int rc = recv(nio, flags);
            if (rc > 0)
                buf.writerIndex(writerIndex + rc);
            return rc;
        } catch (UnsupportedOperationException ex) {
            return recvNettyFallback(buf, writerIndex, writable, flags);
        }
    }

    @Deprecated(forRemoval = false)
    int tryRecv(ByteBuf buf, ReceiveFlag flags) {
        Objects.requireNonNull(buf, "buf");
        Objects.requireNonNull(flags, "flags");
        int writable = buf.writableBytes();
        if (writable <= 0)
            return 0;

        int writerIndex = buf.writerIndex();
        MemorySegment directSeg = nettyWritableSegment(buf, writerIndex, writable);
        if (directSeg.address() != 0) {
            int rc = tryRecv(directSeg, 0, writable, flags);
            if (rc > 0)
                buf.writerIndex(writerIndex + rc);
            return rc;
        }
        try {
            ByteBuffer nio = nettyWritableBuffer(buf, writerIndex, writable);
            int rc = tryRecv(nio, flags);
            if (rc > 0)
                buf.writerIndex(writerIndex + rc);
            return rc;
        } catch (UnsupportedOperationException ex) {
            return tryRecvNettyFallback(buf, writerIndex, writable, flags);
        }
    }

    private static void closeStreamPacket(MemorySegment msg) {
        try {
            NativeMsg.msgClose(msg);
        } catch (RuntimeException ignored) {
        }
    }

    private int onStreamPackets(MemorySegment rid, MemorySegment msgs,
                                long msgCount, MemorySegment userdata) {
        if (msgs == null || msgs.address() == 0 || msgCount <= 0)
            return 0;
        StreamPacketBatchHandler handler = streamBatchHandler;
        if (handler == null || rid == null) {
            try {
                NativeMsg.msgvClose(msgs, msgCount);
            } catch (RuntimeException ignored) {
            }
            return 0;
        }
        if (msgCount > Integer.MAX_VALUE) {
            try {
                NativeMsg.msgvClose(msgs, msgCount);
            } catch (RuntimeException ignored) {
            }
            return 1;
        }

        final long routingId;
        try {
            routingId = decodeStreamRoutingId(rid);
        } catch (RuntimeException ex) {
            try {
                NativeMsg.msgvClose(msgs, msgCount);
            } catch (RuntimeException ignored) {
            }
            return 1;
        }

        Message[] packets;
        try {
            packets = Message.fromOwnedMsgVector(msgs, msgCount);
        } catch (RuntimeException ex) {
            return 1;
        }

        try {
            return handler.onPackets(routingId, packets);
        } catch (Throwable t) {
            Message.closeAll(packets);
            return 1;
        }
    }

    private int onStreamRaw(MemorySegment rid, MemorySegment msg, MemorySegment userdata) {
        if (msg == null || msg.address() == 0)
            return 0;
        StreamPacketHandler handler = streamRawHandler;
        if (handler == null || rid == null) {
            closeStreamPacket(msg);
            return 0;
        }

        final long routingId;
        try {
            routingId = decodeStreamRoutingId(rid);
        } catch (RuntimeException ex) {
            closeStreamPacket(msg);
            return 1;
        }

        final Message payload;
        try {
            payload = Message.fromOwnedNative(msg);
        } catch (Throwable t) {
            closeStreamPacket(msg);
            return 1;
        }

        try {
            return handler.onPacket(routingId, payload);
        } catch (Throwable t) {
            try {
                payload.close();
            } catch (RuntimeException ignored) {
            }
            return 1;
        }
    }

    private int streamSend(MemorySegment routingId, long ridOffset, long ridLength,
                           MemorySegment payload, long payloadOffset,
                           long payloadLength, SendFlag flags) {
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(payload, "payload");
        Objects.requireNonNull(flags, "flags");
        validateRange(routingId.byteSize(), ridOffset, ridLength, "routingId");
        validateRange(payload.byteSize(), payloadOffset, payloadLength, "payload");
        if (ridLength != STREAM_ROUTING_ID_SIZE) {
            throw new IllegalArgumentException(
              "STREAM routingId must be exactly 4 bytes");
        }

        MemorySegment payloadSlice;
        if (payloadLength == 0) {
            payloadSlice = MemorySegment.NULL;
        } else if (payload.isNative()) {
            payloadSlice = payload.asSlice(payloadOffset, payloadLength);
        } else {
            payloadSlice = ensureSendScratch(toIntLength(payloadLength));
            MemorySegment.copy(payload, payloadOffset, payloadSlice, 0,
              payloadLength);
        }

        MemorySegment ridLayout = streamRoutingIdScratch.get();
        ridLayout.set(ValueLayout.JAVA_BYTE, 0, (byte) ridLength);
        MemorySegment.copy(routingId, ridOffset,
          ridLayout.asSlice(1, ridLength), 0, ridLength);
        int rc = Native.streamSend(handle, ridLayout, payloadSlice,
          payloadLength, flags.getValue());
        if (rc < 0)
            throw ZlinkException.fromLastError("zlink_stream_send");
        return rc;
    }

    private int streamSend(byte[] routingId, int ridOffset, int ridLength,
                           byte[] payload, int payloadOffset,
                           int payloadLength, SendFlag flags) {
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(payload, "payload");
        Objects.requireNonNull(flags, "flags");
        validateRange(routingId.length, ridOffset, ridLength, "routingId");
        validateRange(payload.length, payloadOffset, payloadLength, "payload");
        if (ridLength != STREAM_ROUTING_ID_SIZE) {
            throw new IllegalArgumentException(
              "STREAM routingId must be exactly 4 bytes");
        }

        MemorySegment payloadSlice;
        if (payloadLength == 0) {
            payloadSlice = MemorySegment.NULL;
        } else {
            payloadSlice = ensureSendScratch(payloadLength);
            MemorySegment.copy(MemorySegment.ofArray(payload), payloadOffset,
              payloadSlice, 0, payloadLength);
        }

        MemorySegment ridLayout = streamRoutingIdScratch.get();
        ridLayout.set(ValueLayout.JAVA_BYTE, 0, (byte) ridLength);
        MemorySegment.copy(MemorySegment.ofArray(routingId), ridOffset,
          ridLayout.asSlice(1, ridLength), 0, ridLength);
        int rc = Native.streamSend(handle, ridLayout, payloadSlice,
          payloadLength, flags.getValue());
        if (rc < 0)
            throw ZlinkException.fromLastError("zlink_stream_send");
        return rc;
    }

    public MemorySegment handle() {
        return handle;
    }

    public void close() {
        receiveHandler = null;
        subscribeHandler = null;
        sendReadyHandler = null;
        callbackFailure = null;
        closeArena(receiveCallbackArena);
        closeArena(subscribeCallbackArena);
        closeArena(sendReadyCallbackArena);
        receiveCallbackArena = null;
        subscribeCallbackArena = null;
        sendReadyCallbackArena = null;
        receiveCallbackStub = MemorySegment.NULL;
        subscribeCallbackStub = MemorySegment.NULL;
        sendReadyCallbackStub = MemorySegment.NULL;
        if (streamAttached) {
            try {
                Native.streamDetach(handle);
            } catch (RuntimeException ignored) {
            }
            streamAttached = false;
            streamRawHandler = null;
            streamBatchHandler = null;
            closeArena(streamCallbackArena);
            streamCallbackArena = null;
            streamCallbackStub = MemorySegment.NULL;
        }
        if (handle != null && handle.address() != 0) {
            if (own)
                Native.close(handle);
            handle = MemorySegment.NULL;
        }
        closeArena(sendScratchArena);
        closeArena(recvScratchArena);
        sendScratchArena = null;
        recvScratchArena = null;
        sendScratch = MemorySegment.NULL;
        recvScratch = MemorySegment.NULL;
        streamRoutingIdScratch.remove();
    }

    private static void validateRange(int total, int offset, int length, String name) {
        if (offset < 0 || length < 0 || offset > total - length)
            throw new IndexOutOfBoundsException(name + " range out of bounds");
    }

    private static void validateRange(long total, long offset, long length, String name) {
        if (offset < 0 || length < 0 || offset > total - length)
            throw new IndexOutOfBoundsException(name + " range out of bounds");
    }

    private static void validateOptionType(SocketOptionKey<?> option,
                                           SocketOptionValueType expected) {
        if (option.valueType() != expected) {
            throw new IllegalArgumentException(
              option.name() + " expects " + option.valueType()
                + ", not " + expected);
        }
    }

    private void validateAmbiguousOption(SocketOptionKey<?> option) {
        if (option.optionId() != SocketOption.TLS_VERIFY.getValue())
            return;
        SocketType type = resolveSocketType();
        if (type == null)
            return;
        if (type == SocketType.XPUB) {
            if (option != SocketOptions.XPUB_MANUAL_LAST_VALUE) {
                throw new IllegalArgumentException(
                  "XPUB socket option id 98 must use "
                    + SocketOptions.XPUB_MANUAL_LAST_VALUE.name());
            }
            return;
        }
        if (option != SocketOptions.TLS_VERIFY) {
            throw new IllegalArgumentException(
              "Non-XPUB socket option id 98 must use "
                + SocketOptions.TLS_VERIFY.name());
        }
    }

    private SocketType resolveSocketType() {
        if (socketTypeHint != null)
            return socketTypeHint;
        try {
            return SocketType.fromValue(getSockOptInt(SocketOption.TYPE.getValue()));
        } catch (RuntimeException ignored) {
            return null;
        }
    }

    private void setSockOptRaw(int optionId, MemorySegment value, long len) {
        int rc = Native.setSockOpt(handle, translateLegacyCommonOptionId(optionId),
          value, len);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_setsockopt");
    }

    private void setSockOptBytes(int optionId, byte[] value, int offset,
                                 int length) {
        MemorySegment buf = length == 0 ? MemorySegment.NULL
            : ensureSendScratch(length);
        if (length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), offset, buf, 0,
                length);
        }
        setSockOptRaw(optionId, buf, length);
    }

    private void setTypedBytesOption(int optionId, byte[] value, int offset,
                                     int length) {
        if (optionId == SocketOption.ROUTING_ID.getValue()) {
            setRoutingIdBytes(value, offset, length);
            return;
        }
        if (optionId == SocketOption.SUBSCRIBE.getValue()) {
            setSubscriptionBytes(value, offset, length, true);
            return;
        }
        if (optionId == SocketOption.UNSUBSCRIBE.getValue()) {
            setSubscriptionBytes(value, offset, length, false);
            return;
        }
        setSockOptBytes(optionId, value, offset, length);
    }

    @SuppressWarnings("unchecked")
    private <T> T getTypedStringOption(SocketOptionKey<T> option) {
        if (option.optionId() == SocketOption.ROUTING_ID.getValue()) {
            return (T) new String(getRoutingIdBytes(), StandardCharsets.UTF_8);
        }
        return (T) decodeCString(
          getSockOptBytes(option.optionId(), option.maxReadLength()));
    }

    @SuppressWarnings("unchecked")
    private <T> T getTypedBytesOption(SocketOptionKey<T> option) {
        if (option.optionId() == SocketOption.ROUTING_ID.getValue()) {
            return (T) getRoutingIdBytes();
        }
        return (T) getSockOptBytes(option.optionId(), option.maxReadLength());
    }

    private void setRoutingIdBytes(byte[] value, int offset, int length) {
        MemorySegment buf = length == 0 ? MemorySegment.NULL
          : ensureSendScratch(length);
        if (length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), offset, buf, 0,
              length);
        }
        int rc = Native.setRoutingId(handle, buf, length);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_set_routing_id");
    }

    private byte[] getRoutingIdBytes() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment outRid = arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
            int rc = Native.getRoutingId(handle, outRid);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_get_routing_id");
            int size = outRid.get(ValueLayout.JAVA_BYTE,
              NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
            byte[] out = new byte[size];
            if (size > 0) {
                MemorySegment.copy(outRid, NativeLayouts.ROUTING_ID_DATA_OFFSET,
                  MemorySegment.ofArray(out), 0, size);
            }
            return out;
        }
    }

    private void setSubscriptionBytes(byte[] value, int offset, int length,
                                      boolean subscribe) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment filter = length == 0 ? arena.allocate(1)
              : arena.allocate(length + 1L);
            if (length > 0) {
                MemorySegment.copy(MemorySegment.ofArray(value), offset, filter,
                  0, length);
            }
            filter.set(ValueLayout.JAVA_BYTE, length, (byte) 0);
            int rc = subscribe ? Native.setSubscription(handle, filter)
              : Native.unsetSubscription(handle, filter);
            if (rc != 0) {
                throw ZlinkException.fromLastError(subscribe
                  ? "zlink_set_subscription" : "zlink_unset_subscription");
            }
        }
    }

    private void setSockOptInt(int optionId, int value) {
        MemorySegment buf = ensureSendScratch(Integer.BYTES);
        buf.set(ValueLayout.JAVA_INT, 0, value);
        setSockOptRaw(optionId, buf, Integer.BYTES);
    }

    private void setSockOptLong(int optionId, long value) {
        MemorySegment buf = ensureSendScratch(Long.BYTES);
        buf.set(ValueLayout.JAVA_LONG, 0, value);
        setSockOptRaw(optionId, buf, Long.BYTES);
    }

    private byte[] getSockOptBytes(int optionId, int maxLen) {
        if (maxLen < 0)
            throw new IllegalArgumentException("maxLen must be >= 0");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = maxLen == 0 ? MemorySegment.NULL
                : arena.allocate(maxLen);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, maxLen);
            int rc = Native.getSockOpt(handle,
                translateLegacyCommonOptionId(optionId), buf, len);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_getsockopt");
            long actualLong = len.get(ValueLayout.JAVA_LONG, 0);
            if (actualLong < 0)
                actualLong = 0;
            if (actualLong > maxLen)
                actualLong = maxLen;
            int actual = (int) actualLong;
            if (actual == 0)
                return new byte[0];
            byte[] out = new byte[actual];
            MemorySegment.copy(buf, 0, MemorySegment.ofArray(out), 0, actual);
            return out;
        }
    }

    private int getSockOptInt(int optionId) {
        if (optionId == SocketOption.RCVMORE.getValue()) {
            return legacyReceiveState.get().pendingCount() > 0 ? 1 : 0;
        }
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, ValueLayout.JAVA_INT.byteSize());
            int rc = Native.getSockOpt(handle,
                translateLegacyCommonOptionId(optionId), buf, len);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_getsockopt");
            return buf.get(ValueLayout.JAVA_INT, 0);
        }
    }

    void sendMessageFrame(Message message, SendFlag flag) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(flag, "flag");
        withNativeSendFrame(message, nativeMsg -> {
            int rc = Native.sendMultipart(handle, nativeMsg, 1, flag.getValue());
            if (rc < 0)
                throw ZlinkException.fromLastError("zlink_send");
            return null;
        });
    }

    boolean trySendMessageFrame(Message message, SendFlag flag) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(flag, "flag");
        while (true) {
            Integer rc = withNativeSendFrame(message,
              nativeMsg -> Native.sendMultipart(handle, nativeMsg, 1,
                flag.getValue()));
            if (rc >= 0) {
                return true;
            }

            int errno = Native.errno();
            if (errno == ERRNO_EINTR) {
                continue;
            }
            if (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN) {
                return false;
            }
            throw ZlinkException.fromLastError("zlink_send");
        }
    }

    private <T> T withNativeSendFrame(Message message,
                                      NativeFrameAction<T> action) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            Object anchor = message.transferTo(nativeMsg);
            boolean success = false;
            try {
                T result = action.run(nativeMsg);
                success = true;
                return result;
            } finally {
                if (!success) {
                    message.restoreFromNative(nativeMsg, false, anchor);
                    try {
                        NativeMsg.msgClose(nativeMsg);
                    } catch (RuntimeException ignored) {
                    }
                }
            }
        }
    }

    @FunctionalInterface
    private interface NativeFrameAction<T> {
        T run(MemorySegment nativeMsg);
    }

    void recvMessageFrame(Message message, ReceiveFlag flag) {
        Message frame = takeRecvFrame(flag, false);
        try {
            frame.moveInto(message, frame.more());
        } finally {
            frame.close();
        }
    }

    int tryRecvMessageFrame(Message message, ReceiveFlag flag) {
        Message frame = takeRecvFrame(flag, true);
        if (frame == null)
            return -1;
        try {
            return frame.moveInto(message, frame.more());
        } finally {
            frame.close();
        }
    }

    private Message takeRecvFrame(ReceiveFlag flags, boolean nonBlocking) {
        Objects.requireNonNull(flags, "flags");
        LegacyReceiveState state = legacyReceiveState.get();
        while (true) {
            if (state.hasPending()) {
                return state.poll();
            }

            Native.MultipartReceive received = Native.recvMultipart(handle,
              flags.getValue());
            if (received != null) {
                state.replace(materializeFrames(received));
                continue;
            }

            int errno = Native.errno();
            if (errno == ERRNO_EINTR) {
                continue;
            }
            if (nonBlocking
                && (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN)) {
                return null;
            }
            throw ZlinkException.fromLastError("zlink_recv");
        }
    }

    private static Message[] materializeFrames(Native.MultipartReceive received) {
        byte[] routingId = received.routingId();
        Message[] payload = Message.fromMsgVector(received.parts(),
          received.partCount());
        int prefix = routingId == null || routingId.length == 0 ? 0 : 1;
        if (payload.length == 0 && prefix == 0) {
            return new Message[] {Message.fromBytes(EMPTY_BYTES)};
        }
        if (prefix == 0) {
            return payload;
        }
        Message[] frames = new Message[payload.length + 1];
        frames[0] = Message.fromBytes(routingId);
        System.arraycopy(payload, 0, frames, 1, payload.length);
        return frames;
    }

    private void sendParts(RoutingId routingId, List<Message> parts,
                           SendFlag flags, boolean nonBlocking) {
        ensureOpen();
        validateParts(parts);
        while (true) {
            if (trySendPartsOnce(routingId, parts, flags))
                return;
            int errno = Native.errno();
            if (errno == ERRNO_EINTR)
                continue;
            if (nonBlocking
                && (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN)) {
                return;
            }
            throw ZlinkException.fromLastError(
                routingId == null ? "zlink_send" : "zlink_send_rid");
        }
    }

    private boolean trySendPartsOnce(RoutingId routingId, List<Message> parts,
                                     SendFlag flags) {
        try (Arena arena = Arena.ofConfined()) {
            int count = parts.size();
            long msgSize = NativeLayouts.MSG_LAYOUT.byteSize();
            MemorySegment nativeParts = arena.allocate(msgSize * count,
                NativeLayouts.MSG_LAYOUT.byteAlignment());
            Object[] anchors = new Object[count];
            boolean success = false;
            try {
                for (int i = 0; i < count; i++) {
                    MemorySegment nativeMsg = nativeParts.asSlice((long) i * msgSize,
                        msgSize);
                    anchors[i] = parts.get(i).transferTo(nativeMsg);
                }
                int rc = routingId == null
                    ? Native.sendMultipart(handle, nativeParts, count,
                        flags.getValue())
                    : Native.sendMultipart(handle, nativeRoutingId(arena, routingId),
                        nativeParts, count, flags.getValue());
                if (rc >= 0) {
                    success = true;
                    return true;
                }
                return false;
            } finally {
                if (!success) {
                    restoreParts(parts, nativeParts, anchors);
                }
            }
        }
    }

    private void publishParts(String topicId, List<Message> parts,
                              SendFlag flags, boolean nonBlocking) {
        ensureOpen();
        validateParts(parts);
        while (true) {
            if (tryPublishPartsOnce(topicId, parts, flags))
                return;
            int errno = Native.errno();
            if (errno == ERRNO_EINTR)
                continue;
            if (nonBlocking
                && (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN)) {
                return;
            }
            throw ZlinkException.fromLastError("zlink_publish");
        }
    }

    private boolean tryPublishPartsOnce(String topicId, List<Message> parts,
                                        SendFlag flags) {
        try (Arena arena = Arena.ofConfined()) {
            int count = parts.size();
            long msgSize = NativeLayouts.MSG_LAYOUT.byteSize();
            MemorySegment nativeTopic = arena.allocateFrom(topicId,
                StandardCharsets.UTF_8);
            MemorySegment nativeParts = arena.allocate(msgSize * count,
                NativeLayouts.MSG_LAYOUT.byteAlignment());
            Object[] anchors = new Object[count];
            boolean success = false;
            try {
                for (int i = 0; i < count; i++) {
                    MemorySegment nativeMsg = nativeParts.asSlice((long) i * msgSize,
                        msgSize);
                    anchors[i] = parts.get(i).transferTo(nativeMsg);
                }
                int rc = Native.publish(handle, nativeTopic, nativeParts, count,
                    flags.getValue());
                if (rc >= 0) {
                    success = true;
                    return true;
                }
                return false;
            } finally {
                if (!success) {
                    restoreParts(parts, nativeParts, anchors);
                }
            }
        }
    }

    private static void restoreParts(List<Message> parts,
                                     MemorySegment nativeParts,
                                     Object[] anchors) {
        long msgSize = NativeLayouts.MSG_LAYOUT.byteSize();
        for (int i = 0; i < parts.size(); i++) {
            MemorySegment nativeMsg = nativeParts.asSlice((long) i * msgSize,
                msgSize);
            try {
                parts.get(i).restoreFromNative(nativeMsg, i + 1 < parts.size(),
                    anchors[i]);
            } catch (RuntimeException ignored) {
                try {
                    NativeMsg.msgClose(nativeMsg);
                } catch (RuntimeException ignoredClose) {
                }
            }
        }
    }

    private static void validateParts(List<Message> parts) {
        if (parts.isEmpty())
            throw new IllegalArgumentException("parts must not be empty");
        for (int i = 0; i < parts.size(); i++) {
            if (parts.get(i) == null)
                throw new IllegalArgumentException("parts[" + i + "] is null");
        }
    }

    private static RoutingId toRoutingId(byte[] value) {
        if (value == null || value.length == 0)
            return null;
        return RoutingId.copyOf(value);
    }

    private static byte[] decodeRoutingId(MemorySegment nativeRid) {
        int size = nativeRid.get(ValueLayout.JAVA_BYTE,
            NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        if (size == 0)
            return null;
        byte[] value = new byte[size];
        MemorySegment.copy(nativeRid, NativeLayouts.ROUTING_ID_DATA_OFFSET,
            MemorySegment.ofArray(value), 0, size);
        return value;
    }

    private static int normalizeTopicLength(MemorySegment topic, int capacity,
                                            long reportedLength) {
        long len = reportedLength;
        if (len < 0)
            len = 0;
        if (len > capacity)
            len = capacity;
        int bounded = (int) len;
        if (bounded > 0 && topic.get(ValueLayout.JAVA_BYTE, bounded - 1) == 0)
            bounded--;
        return bounded;
    }

    private static MemorySegment nativeRoutingId(Arena arena, RoutingId routingId) {
        byte[] value = routingId.toByteArray();
        MemorySegment nativeRid = arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
        nativeRid.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
            (byte) value.length);
        if (value.length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), 0, nativeRid,
                NativeLayouts.ROUTING_ID_DATA_OFFSET, value.length);
        }
        return nativeRid;
    }

    private byte[] subscriptionAt(long index, MemorySegment lenInOut,
                                  MemorySegment isPatternOut, int initialCapacity) {
        int capacity = Math.max(initialCapacity, 0);
        while (true) {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment filterOut = capacity == 0 ? MemorySegment.NULL
                    : arena.allocate(capacity);
                lenInOut.set(ValueLayout.JAVA_LONG, 0, capacity);
                isPatternOut.set(ValueLayout.JAVA_INT, 0, 0);
                int rc = Native.subscriptionAt(handle, index, filterOut, lenInOut,
                    isPatternOut);
                if (rc == 0) {
                    int actual = toIntLength(lenInOut.get(ValueLayout.JAVA_LONG, 0));
                    if (actual == 0)
                        return new byte[0];
                    byte[] out = new byte[actual];
                    MemorySegment.copy(filterOut, 0, MemorySegment.ofArray(out), 0,
                        actual);
                    return out;
                }
                int errno = Native.errno();
                if (errno == 2)
                    return null;
                if (errno == 22) {
                    capacity = toIntLength(lenInOut.get(ValueLayout.JAVA_LONG, 0));
                    continue;
                }
                throw ZlinkException.fromLastError("zlink_subscription_at");
            }
        }
    }

    private long getSockOptLong(int optionId) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(ValueLayout.JAVA_LONG);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, ValueLayout.JAVA_LONG.byteSize());
            int rc = Native.getSockOpt(handle,
                translateLegacyCommonOptionId(optionId), buf, len);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_getsockopt");
            return buf.get(ValueLayout.JAVA_LONG, 0);
        }
    }

    private static int translateLegacyCommonOptionId(int optionId) {
        return switch (optionId) {
            case 4 -> 0x3001;
            case 8 -> 0x3003;
            case 9 -> 0x3004;
            case 11 -> 0x3005;
            case 12 -> 0x3006;
            case 14 -> 0x3007;
            case 15 -> 0x3008;
            case 16 -> 0x3009;
            case 17 -> 0x300A;
            case 18 -> 0x300B;
            case 19 -> 0x300C;
            case 21 -> 0x300D;
            case 22 -> 0x300E;
            case 23 -> 0x300F;
            case 24 -> 0x3010;
            case 25 -> 0x3011;
            case 27 -> 0x3012;
            case 28 -> 0x3013;
            case 32 -> 0x3014;
            case 34 -> 0x3015;
            case 35 -> 0x3016;
            case 36 -> 0x3017;
            case 37 -> 0x3018;
            case 39 -> 0x3019;
            case 42 -> 0x301A;
            case 54 -> 0x301B;
            case 57 -> 0x301C;
            case 66 -> 0x301D;
            case 70 -> 0x301E;
            case 74 -> 0x3020;
            case 75 -> 0x3021;
            case 76 -> 0x3022;
            case 77 -> 0x3023;
            case 79 -> 0x3024;
            case 80 -> 0x3025;
            case 84 -> 0x3026;
            case 92 -> 0x3027;
            case 95 -> 0x3028;
            case 96 -> 0x3029;
            case 97 -> 0x302A;
            case 98 -> 0x302B;
            case 99 -> 0x302C;
            case 100 -> 0x302D;
            case 101 -> 0x302E;
            case 102 -> 0x302F;
            case 117 -> 0x3030;
            case 118 -> 0x3031;
            default -> optionId;
        };
    }

    private static String decodeCString(byte[] raw) {
        int len = raw.length;
        for (int i = 0; i < raw.length; i++) {
            if (raw[i] == 0) {
                len = i;
                break;
            }
        }
        if (len == 0)
            return "";
        return new String(raw, 0, len, StandardCharsets.UTF_8);
    }

    private MemorySegment ensureSendScratch(int length) {
        if (length <= 0)
            return MemorySegment.NULL;
        if (sendScratch.address() == 0 || sendScratchCapacity < length) {
            closeArena(sendScratchArena);
            sendScratchArena = Arena.ofShared();
            sendScratch = sendScratchArena.allocate(length);
            sendScratchCapacity = length;
        }
        return sendScratch.asSlice(0, length);
    }

    private MemorySegment ensureRecvScratch(int length) {
        if (length <= 0)
            return MemorySegment.NULL;
        if (recvScratch.address() == 0 || recvScratchCapacity < length) {
            closeArena(recvScratchArena);
            recvScratchArena = Arena.ofShared();
            recvScratch = recvScratchArena.allocate(length);
            recvScratchCapacity = length;
        }
        return recvScratch.asSlice(0, length);
    }

    private static ByteBuffer nettyReadableBuffer(ByteBuf buf, int index,
                                                  int length) {
        return buf.nioBufferCount() == 1
            ? buf.internalNioBuffer(index, length)
            : buf.nioBuffer(index, length);
    }

    private static MemorySegment nettyReadableSegment(ByteBuf buf, int index,
                                                      int length) {
        if (length <= 0 || !buf.hasMemoryAddress()) {
            return MemorySegment.NULL;
        }
        return MemorySegment.ofAddress(buf.memoryAddress() + index)
            .reinterpret(length);
    }

    private static ByteBuffer nettyWritableBuffer(ByteBuf buf, int index,
                                                  int length) {
        return buf.nioBufferCount() == 1
            ? buf.internalNioBuffer(index, length)
            : buf.nioBuffer(index, length);
    }

    private static MemorySegment nettyWritableSegment(ByteBuf buf, int index,
                                                      int length) {
        if (length <= 0 || !buf.hasMemoryAddress()) {
            return MemorySegment.NULL;
        }
        return MemorySegment.ofAddress(buf.memoryAddress() + index)
            .reinterpret(length);
    }

    private static void closeArena(Arena arena) {
        if (arena != null && arena.scope().isAlive())
            arena.close();
    }

    private static int toIntLength(long length) {
        if (length > Integer.MAX_VALUE) {
            throw new IllegalArgumentException("length too large: " + length);
        }
        return (int) length;
    }

    private void ensureOpen() {
        if (handle == null || handle.address() == 0)
            throw new IllegalStateException("socket is closed");
        ensureNoCallbackFailure();
    }

    private void ensureNoCallbackFailure() {
        RuntimeException failure = callbackFailure;
        if (failure != null)
            throw failure;
    }

    private MethodHandle callbackHandle(String name, MethodType type) {
        try {
            return MethodHandles.lookup().findVirtual(Socket.class, name, type)
              .bindTo(this);
        } catch (ReflectiveOperationException ex) {
            throw new IllegalStateException("failed to bind callback " + name,
              ex);
        }
    }

    private void handleReceiveCallback(MemorySegment sourceRid,
                                       MemorySegment parts,
                                       long partCount,
                                       MemorySegment userdata) {
        SocketMessageHandler handler = receiveHandler;
        if (handler == null)
            return;
        try (Received received = receivedFromCallback(sourceRid, parts,
               partCount)) {
            handler.onMessage(received);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private void handleSubscribeCallback(MemorySegment sourceRid,
                                         MemorySegment topic,
                                         long topicLen,
                                         MemorySegment parts,
                                         long partCount,
                                         MemorySegment userdata) {
        SubscribeHandler handler = subscribeHandler;
        if (handler == null)
            return;
        try (Received received = receivedFromCallback(sourceRid, parts,
               partCount)) {
            handler.onMessage(readRoutingId(sourceRid), decodeTopic(topic,
                topicLen), received);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private void handleSendReadyCallback(MemorySegment subject,
                                         MemorySegment userdata) {
        SendReadyHandler handler = sendReadyHandler;
        if (handler == null)
            return;
        try {
            handler.onReady();
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private Received receivedFromCallback(MemorySegment sourceRid,
                                          MemorySegment parts,
                                          long partCount) {
        Message[] frames = Message.fromOwnedMsgVector(parts, partCount);
        boolean closed = false;
        try {
            NativeMsg.multipartClose(parts, partCount);
            closed = true;
            return new Received(readRoutingId(sourceRid), frames);
        } finally {
            if (!closed)
                Message.closeAll(frames);
        }
    }

    private static RoutingId readRoutingId(MemorySegment sourceRid) {
        if (sourceRid == null || sourceRid.address() == 0)
            return null;
        MemorySegment routingId = sourceRid.reinterpret(
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize());
        int size = routingId.get(ValueLayout.JAVA_BYTE,
            NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        if (size == 0)
            return null;
        byte[] value = new byte[size];
        MemorySegment.copy(routingId, NativeLayouts.ROUTING_ID_DATA_OFFSET,
            MemorySegment.ofArray(value), 0, size);
        return RoutingId.copyOf(value);
    }

    private static String decodeTopic(MemorySegment topic, long topicLen) {
        int length = toIntLength(topicLen);
        if (length == 0)
            return "";
        MemorySegment topicBytes = topic.reinterpret(length);
        if (length > 0
          && topicBytes.get(ValueLayout.JAVA_BYTE, length - 1) == 0) {
            length--;
        }
        if (length == 0)
            return "";
        return new String(topicBytes.asSlice(0, length).toArray(
            ValueLayout.JAVA_BYTE), StandardCharsets.UTF_8);
    }

    private void recordCallbackFailure(RuntimeException failure) {
        callbackFailure = failure;
        Thread current = Thread.currentThread();
        Thread.UncaughtExceptionHandler uncaught = current.getUncaughtExceptionHandler();
        if (uncaught != null) {
            uncaught.uncaughtException(current, failure);
        }
    }

    private int sendNettyFallback(ByteBuf buf,
                                  int readerIndex,
                                  int length,
                                  int sendFlags) {
        byte[] tmp = new byte[length];
        buf.getBytes(readerIndex, tmp);
        int rc = send(tmp, 0, length, sendFlags);
        if (rc > 0)
            buf.readerIndex(readerIndex + rc);
        return rc;
    }

    private boolean trySendNettyFallback(ByteBuf buf,
                                         int readerIndex,
                                         int length,
                                         int sendFlags) {
        byte[] tmp = new byte[length];
        buf.getBytes(readerIndex, tmp);
        boolean sent = trySend(tmp, 0, length, sendFlags);
        if (sent)
            buf.readerIndex(readerIndex + length);
        return sent;
    }

    private int recvNettyFallback(ByteBuf buf,
                                  int writerIndex,
                                  int writable,
                                  ReceiveFlag flags) {
        try (Message frame = takeRecvFrame(flags, false)) {
            int rc = Math.min(writable, frame.size());
            if (rc > 0) {
                ByteBuffer src = frame.dataSegment().asSlice(0, rc).asByteBuffer();
                buf.setBytes(writerIndex, src);
                buf.writerIndex(writerIndex + rc);
            }
            return rc;
        }
    }

    private int tryRecvNettyFallback(ByteBuf buf,
                                     int writerIndex,
                                     int writable,
                                     ReceiveFlag flags) {
        try (Message frame = takeRecvFrame(flags, true)) {
            if (frame == null)
                return -1;
            int rc = Math.min(writable, frame.size());
            if (rc > 0) {
                ByteBuffer src = frame.dataSegment().asSlice(0, rc).asByteBuffer();
                buf.setBytes(writerIndex, src);
                buf.writerIndex(writerIndex + rc);
            }
            return rc;
        }
    }

    private static void validateStreamRoutingId(long routingId) {
        if ((routingId & ~STREAM_ROUTING_ID_MAX) != 0L) {
            throw new IllegalArgumentException(
              "STREAM routingId must be an unsigned 32-bit value");
        }
    }

    private static void writeStreamRoutingId(MemorySegment ridLayout,
                                             long routingId) {
        ridLayout.set(ValueLayout.JAVA_BYTE, 0, (byte) STREAM_ROUTING_ID_SIZE);
        ridLayout.set(ValueLayout.JAVA_BYTE, 1,
          (byte) ((routingId >>> 24) & 0xFF));
        ridLayout.set(ValueLayout.JAVA_BYTE, 2,
          (byte) ((routingId >>> 16) & 0xFF));
        ridLayout.set(ValueLayout.JAVA_BYTE, 3,
          (byte) ((routingId >>> 8) & 0xFF));
        ridLayout.set(ValueLayout.JAVA_BYTE, 4, (byte) (routingId & 0xFF));
    }

    private static long decodeStreamRoutingId(byte[] routingId) {
        Objects.requireNonNull(routingId, "routingId");
        if (routingId.length != STREAM_ROUTING_ID_SIZE) {
            throw new IllegalStateException(
              "expected 4-byte STREAM routingId but got " + routingId.length);
        }
        return ((routingId[0] & 0xFFL) << 24)
          | ((routingId[1] & 0xFFL) << 16)
          | ((routingId[2] & 0xFFL) << 8)
          | (routingId[3] & 0xFFL);
    }

    private static long decodeStreamRoutingId(MemorySegment routingIdLayout) {
        Objects.requireNonNull(routingIdLayout, "routingIdLayout");
        MemorySegment rid = routingIdLayout.reinterpret(STREAM_ROUTING_ID_LAYOUT_SIZE);
        int ridLen = rid.get(ValueLayout.JAVA_BYTE, 0) & 0xFF;
        if (ridLen != STREAM_ROUTING_ID_SIZE) {
            throw new IllegalStateException(
              "expected 4-byte STREAM routingId but got " + ridLen);
        }
        return ((rid.get(ValueLayout.JAVA_BYTE, 1) & 0xFFL) << 24)
          | ((rid.get(ValueLayout.JAVA_BYTE, 2) & 0xFFL) << 16)
          | ((rid.get(ValueLayout.JAVA_BYTE, 3) & 0xFFL) << 8)
          | (rid.get(ValueLayout.JAVA_BYTE, 4) & 0xFFL);
    }
}
