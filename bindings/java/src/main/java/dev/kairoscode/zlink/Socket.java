/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import dev.kairoscode.zlink.SocketOptionKey;
import dev.kairoscode.zlink.SocketOptions;
import dev.kairoscode.zlink.SocketOptionValueType;
import dev.kairoscode.zlink.service.discovery.Discovery;
import io.netty.buffer.ByteBuf;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Objects;
import java.util.Optional;

/**
 * Abstract common socket base for zlink typed socket facades.
 */
public abstract class Socket implements AutoCloseable {
    static final int DEFAULT_IO_BUFFER_SIZE = 8192;
    static final int ERRNO_EINTR = 4;
    static final int ERRNO_EAGAIN = 11;
    static final int ERRNO_EWOULDBLOCK_WIN = 10035;
    static final int ERRNO_ENOTCONN = 107;
    static final int ERRNO_ENOTCONN_WIN = 10057;
    static final int ERRNO_EHOSTUNREACH = 113;
    static final int ERRNO_EHOSTUNREACH_WIN = 10065;
    static final int ERRNO_ETIMEDOUT = 110;
    static final int ERRNO_ETIMEDOUT_WIN = 10060;
    static final int TOPIC_CAPACITY = 256;
    private static final byte[] EMPTY_BYTES = new byte[0];

    private final SocketCore socketCore;
    private final MessagePlane messagePlane;
    private final TopicPlane topicPlane;
    private final CommonSocketOptions options;
    private MemorySegment handle;
    private final boolean own;
    private final SocketType socketTypeHint;
    private final ThreadLocal<SendScratch> sendScratch =
      ThreadLocal.withInitial(SendScratch::new);
    private final ThreadLocal<LegacyReceiveState> legacyReceiveState =
      ThreadLocal.withInitial(LegacyReceiveState::new);
    private final ThreadLocal<Received> activeLazyReceive =
      new ThreadLocal<>();

    private enum OptionFamily {
        COMMON, ROUTER, PUB, SUB, STREAM
    }

    public void disconnectRid(RoutingId peerRid) {
        socketCore.disconnectRid(peerRid);
    }

    private record OptionRoute(OptionFamily family, int optionId) {
    }

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

    private static final class SendScratch {
        private final Arena arena = Arena.ofConfined();
        private final MemorySegment nativeMsg = arena.allocate(
            NativeLayouts.MSG_LAYOUT);
        private final MemorySegment nativeRoutingId = arena.allocate(
            NativeLayouts.ROUTING_ID_LAYOUT);
    }

    Socket(Context ctx, SocketType type) {
        this.handle = Native.socket(ctx.handle(), type.getValue());
        if (handle == null || handle.address() == 0)
            throw ZlinkException.fromLastError("zlink_socket");
        this.own = true;
        this.socketTypeHint = type;
        this.socketCore = new SocketCore(this);
        this.messagePlane = new MessagePlane(this);
        this.topicPlane = new TopicPlane(this);
        this.options = new CommonSocketOptions(this);
    }

    Socket(MemorySegment handle, boolean own, SocketType socketTypeHint) {
        this.handle = handle;
        this.own = own;
        this.socketTypeHint = socketTypeHint;
        this.socketCore = new SocketCore(this);
        this.messagePlane = new MessagePlane(this);
        this.topicPlane = new TopicPlane(this);
        this.options = new CommonSocketOptions(this);
    }

    /** Binds the socket to the endpoint. */
    void bind(String endpoint) {
        socketCore.bind(endpoint);
    }

    /** Connects the socket to the endpoint. */
    void connect(String endpoint) {
        socketCore.connect(endpoint);
    }

    /** Unbinds the socket from the endpoint. */
    void unbind(String endpoint) {
        socketCore.unbind(endpoint);
    }

    /** Disconnects the socket from the endpoint. */
    void disconnect(String endpoint) {
        socketCore.disconnect(endpoint);
    }

    /** Attaches a fixed-service discovery view to the socket. */
    void attachDiscovery(Discovery discovery) {
        socketCore.attachDiscovery(discovery);
    }

    void setChannelName(String channelName) {
        Objects.requireNonNull(channelName, "channelName");
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.socketSetChannelName(handle,
              NativeHelpers.toCString(arena, channelName));
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_socket_set_channel_name");
            }
        }
    }

    String getChannelName() {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buffer = arena.allocate(256);
            MemorySegment lengthOut = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.socketGetChannelName(handle, buffer, 256,
              lengthOut);
            if (rc != 0) {
                throw ZlinkException.fromLastError(
                  "zlink_socket_get_channel_name");
            }
            int len = (int) lengthOut.get(ValueLayout.JAVA_LONG, 0);
            if (len <= 0) {
                return "";
            }
            return NativeHelpers.fromCString(buffer, len);
        }
    }

    void attachStreamRaw(StreamPacketHandler handler) {
        socketCore.attachStreamRaw(handler);
    }

    void attachStreamRaw(StreamUInt32RawNativeHandler handler) {
        socketCore.attachStreamRaw(handler);
    }

    void attachStreamPacket(StreamFramedPacketHandler handler) {
        socketCore.attachStreamPacket(handler);
    }

    void attachStreamPacket(StreamUInt32FramedPacketHandler handler) {
        socketCore.attachStreamPacket(handler);
    }

    void attachStreamPacket(StreamUInt32FramedNativeHandler handler) {
        socketCore.attachStreamPacket(handler);
    }

    void detachStream() {
        socketCore.detachStream();
    }

    void setSockOpt(SocketOption option, byte[] value) {
        socketCore.setSockOpt(option, value);
    }

    void setSockOpt(SocketOption option, byte[] value, int offset, int length) {
        socketCore.setSockOpt(option, value, offset, length);
    }

    void setSockOpt(SocketOption option, ByteBuffer value) {
        socketCore.setSockOpt(option, value);
    }

    void setSockOpt(SocketOption option, int value) {
        socketCore.setSockOpt(option, value);
    }

    byte[] getSockOptBytes(SocketOption option, int maxLen) {
        return socketCore.getSockOptBytes(option, maxLen);
    }

    int getSockOptInt(SocketOption option) {
        return socketCore.getSockOptInt(option);
    }

    void setOption(SocketOptionKey<Integer> option, int value) {
        socketCore.setOption(option, value);
    }

    void setOption(SocketOptionKey<Long> option, long value) {
        socketCore.setOptionLong(option, value);
    }

    void setOption(SocketOptionKey<String> option, String value) {
        socketCore.setOptionString(option, value);
    }

    void setOption(SocketOptionKey<byte[]> option, byte[] value) {
        socketCore.setOptionBytes(option, value);
    }

    @SuppressWarnings("unchecked")
    <T> T getOption(SocketOptionKey<T> option) {
        return socketCore.getOption(option);
    }

    CommonSocketOptions options() {
        return options;
    }

    /** Opens a socket monitor for all events. */
    public MonitorSocket monitorOpen() {
        return monitorOpen(MonitorEventType.ALL);
    }

    /** Opens a socket monitor for the requested event types. */
    public MonitorSocket monitorOpen(MonitorEventType... events) {
        return socketCore.monitorOpen(resolveMonitorEvents(events));
    }

    public final void setTlsServer(String certPem, String keyPem,
                                   boolean requireClientCert) {
        socketCore.setTlsServer(certPem, keyPem, requireClientCert);
    }

    public final void setTlsClient(String caCertPem, String hostname,
                                   boolean trustSystem) {
        socketCore.setTlsClient(caCertPem, hostname, trustSystem);
    }

    private static int resolveMonitorEvents(MonitorEventType... events) {
        if (events == null || events.length == 0) {
            return MonitorEventType.ALL.getValue();
        }
        int mask = 0;
        for (MonitorEventType event : events) {
            Objects.requireNonNull(event, "events");
            mask |= event.getValue();
        }
        return mask;
    }

    boolean send(Message part) {
        messagePlane.send(part);
        return true;
    }

    boolean send(Message part, SendFlag flags) {
        Objects.requireNonNull(flags, "flags");
        if ((flags.getValue() & SendFlag.DONTWAIT.getValue()) != 0) {
            return trySendResult(sendNoWaitResult(part));
        }
        messagePlane.send(part, flags);
        return true;
    }

    void sendMessageFrame(RoutingId routingId, Message message, SendFlag flag) {
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(flag, "flag");
        ensureBlockingSendAllowed(flag);
        while (true) {
            int rc = sendPartOnce(message, routingId, flag.getValue(),
                Native.PART_FINAL);
            if (rc == 0)
                return;
            int errno = Native.errno();
            if (errno == ERRNO_EINTR)
                continue;
            throwPartSubmitFailure("zlink_send_part_rid");
        }
    }

    boolean send(List<Message> parts) {
        messagePlane.send(parts);
        return true;
    }

    boolean send(List<Message> parts, SendFlag flags) {
        Objects.requireNonNull(flags, "flags");
        if ((flags.getValue() & SendFlag.DONTWAIT.getValue()) != 0) {
            return trySendResult(sendNoWaitResult(parts));
        }
        messagePlane.send(parts, flags);
        return true;
    }

    SendResult sendNoWaitResult(Message part) {
        return messagePlane.sendNoWaitResult(part);
    }

    SendResult sendMessageFrameNoWaitResult(RoutingId routingId, Message message) {
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(message, "message");
        while (true) {
            int rc = sendPartOnce(message, routingId,
                SendFlag.DONTWAIT.getValue(), Native.PART_FINAL);
            if (rc == 0)
                return SendResult.SENT;
            int errno = Native.errno();
            if (errno == ERRNO_EINTR)
                continue;
            return classifyNonBlockingSendErrno("zlink_send_part_rid");
        }
    }

    SendResult sendNoWaitResult(List<Message> parts) {
        return messagePlane.sendNoWaitResult(parts);
    }

    boolean send(RoutingId rid, Message part) {
        messagePlane.send(rid, part);
        return true;
    }

    boolean send(RoutingId rid, Message part, SendFlag flags) {
        Objects.requireNonNull(flags, "flags");
        if ((flags.getValue() & SendFlag.DONTWAIT.getValue()) != 0) {
            return trySendResult(sendNoWaitResult(rid, part));
        }
        messagePlane.send(rid, part, flags);
        return true;
    }

    void send(int rid, Message part, SendFlag flags) {
        Objects.requireNonNull(part, "part");
        Objects.requireNonNull(flags, "flags");
        ensureBlockingSendAllowed(flags);
        int effectiveFlags = flags.getValue();
        int rc = Native.sendMultipartU32(handle, rid, part.nativeHandle(), 1,
            effectiveFlags);
        if (rc < 0)
            throw ZlinkException.fromLastError("zlink_java_send_u32");
        part.markTransferred();
    }

    int send(int rid, MemorySegment payload, int length, int sendFlags) {
        Objects.requireNonNull(payload, "payload");
        SendFlag flag = SendFlag.fromValue(sendFlags);
        return sendDirectSegment(rid, payload, length, flag);
    }

    int sendCopied(int rid, MemorySegment payload, int length, int sendFlags) {
        Objects.requireNonNull(payload, "payload");
        SendFlag flag = SendFlag.fromValue(sendFlags);
        ensureBlockingSendAllowed(flag);
        SendScratch scratch = sendScratch.get();
        MemorySegment nativeMsg = scratch.nativeMsg;
        int rc = NativeMsg.msgInitSize(nativeMsg, length);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_msg_init_size");
        if (length > 0) {
            MemorySegment dst = NativeMsg.msgData(nativeMsg).reinterpret(length);
            MemorySegment.copy(payload, 0, dst, 0, length);
        }
        boolean success = false;
        try {
            rc = Native.sendMultipartU32(handle, rid, nativeMsg, 1,
                flag.getValue());
            if (rc < 0)
                throw ZlinkException.fromLastError("zlink_java_send_u32");
            success = true;
        } finally {
            if (!success) {
                try {
                    NativeMsg.msgClose(nativeMsg);
                } catch (RuntimeException ignored) {
                }
            }
        }
        return length;
    }

    private int sendDirectSegment(int rid, MemorySegment payload, int length,
                                  SendFlag flag) {
        ensureBlockingSendAllowed(flag);
        SendScratch scratch = sendScratch.get();
        MemorySegment nativeMsg = scratch.nativeMsg;
        int rc = NativeMsg.msgInitData(nativeMsg, payload, length,
            MemorySegment.NULL, MemorySegment.NULL);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_msg_init_data");
        boolean success = false;
        try {
            rc = Native.sendMultipartU32(handle, rid, nativeMsg, 1,
                flag.getValue());
            if (rc < 0)
                throw ZlinkException.fromLastError("zlink_java_send_u32");
            success = true;
        } finally {
            if (!success) {
                try {
                    NativeMsg.msgClose(nativeMsg);
                } catch (RuntimeException ignored) {
                }
            }
        }
        return length;
    }

    boolean send(RoutingId rid, List<Message> parts) {
        messagePlane.send(rid, parts);
        return true;
    }

    boolean send(RoutingId rid, List<Message> parts, SendFlag flags) {
        Objects.requireNonNull(flags, "flags");
        if ((flags.getValue() & SendFlag.DONTWAIT.getValue()) != 0) {
            return trySendResult(sendNoWaitResult(rid, parts));
        }
        messagePlane.send(rid, parts, flags);
        return true;
    }

    SendResult sendNoWaitResult(RoutingId rid, Message part) {
        return messagePlane.sendNoWaitResult(rid, part);
    }

    SendResult sendNoWaitResult(RoutingId rid, List<Message> parts) {
        return messagePlane.sendNoWaitResult(rid, parts);
    }

    /** Publishes a single payload part to a topic-aware socket. */
    boolean publish(String topicId, Message part) {
        topicPlane.publish(topicId, part);
        return true;
    }

    /** Publishes a single payload part with explicit send flags. */
    boolean publish(String topicId, Message part, SendFlag flags) {
        Objects.requireNonNull(flags, "flags");
        if ((flags.getValue() & SendFlag.DONTWAIT.getValue()) != 0) {
            return trySendResult(publishNoWaitResult(topicId, part));
        }
        topicPlane.publish(topicId, part, flags);
        return true;
    }

    void publishMessageFrame(String topicId, Message message, SendFlag flags) {
        Objects.requireNonNull(topicId, "topicId");
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(flags, "flags");
        ensureBlockingSendAllowed(flags);
        while (true) {
            int rc = publishPartOnce(topicId, message, flags.getValue(),
                Native.PART_FINAL);
            if (rc == 0)
                return;
            int errno = Native.errno();
            if (errno == ERRNO_EINTR)
                continue;
            throwPartSubmitFailure("zlink_publish_part");
        }
    }

    /** Publishes a multipart payload to a topic-aware socket. */
    boolean publish(String topicId, List<Message> parts) {
        topicPlane.publish(topicId, parts);
        return true;
    }

    /** Publishes a multipart payload with explicit send flags. */
    boolean publish(String topicId, List<Message> parts, SendFlag flags) {
        Objects.requireNonNull(flags, "flags");
        if ((flags.getValue() & SendFlag.DONTWAIT.getValue()) != 0) {
            return trySendResult(publishNoWaitResult(topicId, parts));
        }
        topicPlane.publish(topicId, parts, flags);
        return true;
    }

    SendResult publishNoWaitResult(String topicId, Message part) {
        return topicPlane.publishNoWaitResult(topicId, part);
    }

    SendResult publishMessageFrameNoWaitResult(String topicId, Message message) {
        Objects.requireNonNull(topicId, "topicId");
        Objects.requireNonNull(message, "message");
        while (true) {
            int rc = publishPartOnce(topicId, message,
                SendFlag.DONTWAIT.getValue(), Native.PART_FINAL);
            if (rc == 0)
                return SendResult.SENT;
            int errno = Native.errno();
            if (errno == ERRNO_EINTR)
                continue;
            return classifyNonBlockingSendErrno("zlink_publish_part");
        }
    }

    SendResult publishNoWaitResult(String topicId, List<Message> parts) {
        return topicPlane.publishNoWaitResult(topicId, parts);
    }

    Received recv() {
        return messagePlane.recv();
    }

    Received recv(ReceiveFlag flags) {
        Objects.requireNonNull(flags, "flags");
        if (flags == ReceiveFlag.DONTWAIT) {
            return messagePlane.recvNoWaitOrNull();
        }
        return messagePlane.recv(flags);
    }

    Received recvNoWaitOrNull() {
        return messagePlane.recvNoWaitOrNull();
    }

    Optional<Received> recvNoWait() {
        return Optional.ofNullable(recvNoWaitOrNull());
    }

    Received recvLazy(ReceiveFlag flags) {
        Received received = recvLazyOrNull(flags, false);
        if (received == null) {
            throw new RecvException(RecvResult.NO_DATA, ERRNO_EAGAIN);
        }
        return received;
    }

    Received recvLazyNoWaitOrNull() {
        return recvLazyOrNull(ReceiveFlag.DONTWAIT, true);
    }

    private Received recvLazyOrNull(ReceiveFlag flags, boolean allowNoData) {
        Objects.requireNonNull(flags, "flags");
        prepareRecvLikeOperation();
        while (true) {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment sourceRidOut = arena.allocate(ValueLayout.ADDRESS);
                MemorySegment hasMoreOut = arena.allocate(ValueLayout.JAVA_INT);
                Message firstPart = new Message();
                boolean success = false;
                try {
                    int rc = Native.recvPart(handle, sourceRidOut,
                        firstPart.nativeHandle(), hasMoreOut, flags.getValue());
                    if (rc == 0) {
                        success = true;
                        boolean hasMore =
                            hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                        firstPart.finishReceive(hasMore);
                        byte[] routingId = decodeRoutingIdPtr(
                            sourceRidOut.get(ValueLayout.ADDRESS, 0));
                        Received.PartCursor cursor = hasMore
                            ? new BasicReceiveCursor(flags.getValue())
                            : null;
                        Received[] ref = new Received[1];
                        Runnable onTerminal = () -> {
                            Received active = activeLazyReceive.get();
                            if (active == ref[0]) {
                                activeLazyReceive.remove();
                            }
                        };
                        Received received = new Received(routingId, null,
                            firstPart, cursor, 0L, false, null, onTerminal);
                        ref[0] = received;
                        return registerLazyReceive(received, hasMore);
                    }
                } finally {
                    if (!success) {
                        try {
                            firstPart.close();
                        } catch (RuntimeException ignored) {
                        }
                    }
                }
            }

            int errno = Native.errno();
            if (errno == ERRNO_EINTR)
                continue;
            if (allowNoData
                && (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN)) {
                return null;
            }
            throw ZlinkException.fromLastError("zlink_recv_part");
        }
    }

    /** Receives a topic-aware delivery from a SUB/XSUB-style socket. */
    TopicMessage subscribe() {
        return topicPlane.subscribe();
    }

    /** Receives a topic-aware delivery with explicit receive flags. */
    TopicMessage subscribe(ReceiveFlag flags) {
        Objects.requireNonNull(flags, "flags");
        if (flags == ReceiveFlag.DONTWAIT) {
            return topicPlane.subscribeNoWait().orElse(null);
        }
        return topicPlane.subscribe(flags);
    }

    Optional<TopicMessage> subscribeNoWait() {
        return topicPlane.subscribeNoWait();
    }

    SubscriptionEvent receiveSubscriptionEvent() {
        return topicPlane.subscriptionEvent(ReceiveFlag.NONE);
    }

    SubscriptionEvent receiveSubscriptionEvent(ReceiveFlag flags) {
        return topicPlane.subscriptionEvent(flags);
    }

    SubscriptionEvent subscriptionEvent(ReceiveFlag flags) {
        return topicPlane.subscriptionEvent(flags);
    }

    Optional<SubscriptionEvent> tryReceiveSubscriptionEvent() {
        return topicPlane.trySubscriptionEvent();
    }

    void setRoutingId(RoutingId rid) {
        topicPlane.setRoutingId(rid);
    }

    RoutingId routingId() {
        return topicPlane.routingId();
    }

    void setSubscription(String filter) {
        topicPlane.setSubscription(filter);
    }

    void setSubscription(byte[] filter) {
        topicPlane.setSubscription(filter);
    }

    void unsetSubscription(String filter) {
        topicPlane.unsetSubscription(filter);
    }

    void unsetSubscription(byte[] filter) {
        topicPlane.unsetSubscription(filter);
    }

    List<SubscriptionEntry> subscriptions() {
        return topicPlane.subscriptions();
    }

    void onReceive(SocketMessageHandler handler) {
        socketCore.onReceive(handler);
    }

    void onSubscribe(SubscribeHandler handler) {
        socketCore.onSubscribe(handler);
    }

    void onSendReady(SendReadyHandler handler) {
        socketCore.onSendReady(handler);
    }

    private final class BasicReceiveCursor implements Received.PartCursor {
        private final int flags;
        private final Arena arena = Arena.ofConfined();
        private final MemorySegment sourceRidOut = arena.allocate(
            ValueLayout.ADDRESS);
        private final MemorySegment hasMoreOut = arena.allocate(
            ValueLayout.JAVA_INT);
        private boolean hasMore = true;
        private boolean closed;

        private BasicReceiveCursor(int flags) {
            this.flags = flags;
        }

        @Override
        public Message nextPartOrNull() {
            if (closed || !hasMore)
                return null;
            while (true) {
                Message next = new Message();
                boolean success = false;
                try {
                    int rc = Native.recvPart(handle, sourceRidOut,
                        next.nativeHandle(), hasMoreOut, flags);
                    if (rc == 0) {
                        success = true;
                        hasMore = hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                        next.finishReceive(hasMore);
                        if (!hasMore) {
                            closeArena();
                        }
                        return next;
                    }
                } finally {
                    if (!success) {
                        try {
                            next.close();
                        } catch (RuntimeException ignored) {
                        }
                    }
                }

                int errno = Native.errno();
                if (errno == ERRNO_EINTR)
                    continue;
                closeArena();
                throw ZlinkException.fromLastError("zlink_recv_part");
            }
        }

        @Override
        public void close() {
            if (closed)
                return;
            while (hasMore) {
                Message next = nextPartOrNull();
                if (next == null)
                    break;
                try {
                    next.close();
                } catch (RuntimeException ignored) {
                }
            }
            closed = true;
            closeArena();
        }

        private void closeArena() {
            hasMore = false;
            if (arena.scope().isAlive()) {
                arena.close();
            }
        }
    }

    int send(byte[] data, int offset, int length, int sendFlags) {
        Objects.requireNonNull(data, "data");
        validateRange(data.length, offset, length, "data");
        try (Message msg = Message.copyOf(data, offset, length)) {
            sendMessageFrame(msg, SendFlag.fromValue(sendFlags));
            return length;
        }
    }

    boolean sendNoWaitResult(byte[] data, int offset, int length, int sendFlags) {
        Objects.requireNonNull(data, "data");
        validateRange(data.length, offset, length, "data");
        try (Message msg = Message.copyOf(data, offset, length)) {
            return sendMessageFrameNoWaitResult(msg, SendFlag.fromValue(sendFlags));
        }
    }

    int send(MemorySegment segment, long offset, long length,
             int sendFlags) {
        Objects.requireNonNull(segment, "segment");
        validateRange(segment.byteSize(), offset, length, "segment");
        try (Message msg = segment.isNative()
            ? Message.wrapNative(segment, offset, length)
            : Message.copyOf(segment, offset, length)) {
            sendMessageFrame(msg, SendFlag.fromValue(sendFlags));
            return (int) length;
        }
    }

    boolean sendNoWaitResult(MemorySegment segment, long offset, long length,
                    int sendFlags) {
        Objects.requireNonNull(segment, "segment");
        validateRange(segment.byteSize(), offset, length, "segment");
        try (Message msg = segment.isNative()
            ? Message.wrapNative(segment, offset, length)
            : Message.copyOf(segment, offset, length)) {
            return sendMessageFrameNoWaitResult(msg, SendFlag.fromValue(sendFlags));
        }
    }

    int send(ByteBuf buf, int sendFlags) {
        Objects.requireNonNull(buf, "buf");
        int len = buf.readableBytes();
        if (len <= 0) {
            try (Message msg = Message.copyOf(EMPTY_BYTES)) {
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
        return sendNettyFallback(buf, readerIndex, len, sendFlags);
    }

    boolean sendNoWaitResult(ByteBuf buf, int sendFlags) {
        Objects.requireNonNull(buf, "buf");
        int len = buf.readableBytes();
        if (len <= 0) {
            try (Message msg = Message.copyOf(EMPTY_BYTES)) {
                return sendMessageFrameNoWaitResult(msg, SendFlag.fromValue(sendFlags));
            }
        }

        int readerIndex = buf.readerIndex();
        MemorySegment directSeg = nettyReadableSegment(buf, readerIndex, len);
        if (directSeg.address() != 0) {
            boolean sent = sendNoWaitResult(directSeg, 0, len, sendFlags);
            if (sent) {
                buf.readerIndex(readerIndex + len);
            }
            return sent;
        }
        return sendNettyFallbackNoWait(buf, readerIndex, len, sendFlags);
    }

    int recv(MemorySegment segment, long offset, long length,
             ReceiveFlag flags) {
        Objects.requireNonNull(segment, "segment");
        validateRange(segment.byteSize(), offset, length, "segment");
        if (length == 0)
            return 0;
        try (Message frame = nextRecvFrame(flags, false)) {
            int rc = Math.min(toIntLength(length), frame.size());
            if (rc > 0) {
                MemorySegment.copy(frame.dataSegment(), 0, segment, offset, rc);
            }
            return rc;
        }
    }

    int recvNoWait(MemorySegment segment, long offset, long length,
                ReceiveFlag flags) {
        Objects.requireNonNull(segment, "segment");
        validateRange(segment.byteSize(), offset, length, "segment");
        if (length == 0)
            return 0;
        try (Message frame = nextRecvFrame(flags, true)) {
            if (frame == null)
                return -1;
            int rc = Math.min(toIntLength(length), frame.size());
            if (rc > 0) {
                MemorySegment.copy(frame.dataSegment(), 0, segment, offset, rc);
            }
            return rc;
        }
    }


    MemorySegment handle() {
        return handle;
    }

    public void close() {
        socketCore.close();
    }

    void closeInternal() {
        if (socketCore.discoveryAttached()) {
            throw ZlinkException.fromErrno("zlink_close",
                ErrorCode.EFSM.getValue());
        }
        if (handle != null && handle.address() != 0) {
            if (own) {
                int rc = Native.close(handle);
                if (rc != 0)
                    throw ZlinkException.fromLastError("zlink_close");
            }
            socketCore.closeCommonState();
            handle = MemorySegment.NULL;
            return;
        }
        socketCore.closeCommonState();
    }

    @SuppressWarnings("unchecked")
    <T> T readOption(SocketOptionKey<T> option) {
        return switch (option.valueType()) {
            case INT32 -> (T) Integer.valueOf(getSockOptInt(option.optionId()));
            case INT64 -> (T) Long.valueOf(getSockOptLong(option.optionId()));
            case STRING -> (T) getTypedStringOption(option);
            case BYTES -> (T) getTypedBytesOption(option);
        };
    }

    int recvByteBufDirect(ByteBuf buf, ReceiveFlag flags) {
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
        return recvNettyFallback(buf, writerIndex, writable, flags);
    }

    int recvByteBufDirectNoWait(ByteBuf buf, ReceiveFlag flags) {
        int writable = buf.writableBytes();
        if (writable <= 0)
            return 0;

        int writerIndex = buf.writerIndex();
        MemorySegment directSeg = nettyWritableSegment(buf, writerIndex, writable);
        if (directSeg.address() != 0) {
            int rc = recvNoWait(directSeg, 0, writable, flags);
            if (rc > 0)
                buf.writerIndex(writerIndex + rc);
            return rc;
        }
        return recvNettyFallbackNoWait(buf, writerIndex, writable, flags);
    }

    static void validateRange(int total, int offset, int length, String name) {
        if (offset < 0 || length < 0 || offset > total - length)
            throw new IndexOutOfBoundsException(name + " range out of bounds");
    }

    static void validateRange(long total, long offset, long length, String name) {
        if (offset < 0 || length < 0 || offset > total - length)
            throw new IndexOutOfBoundsException(name + " range out of bounds");
    }

    static void validateOptionType(SocketOptionKey<?> option,
                                           SocketOptionValueType expected) {
        if (option.valueType() != expected) {
            throw new IllegalArgumentException(
              option.name() + " expects " + option.valueType()
                + ", not " + expected);
        }
    }

    void validateAmbiguousOption(SocketOptionKey<?> option) {
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

    void validateOptionAccess(int optionId, String optionName) {
        SocketType type = resolveSocketType();
        if (type == null)
            return;
        if (optionId == SocketOption.TLS_VERIFY.getValue()) {
            if (type == SocketType.XPUB) {
                if (SocketOptions.XPUB_MANUAL_LAST_VALUE.name().equals(optionName))
                    return;
                throw new IllegalArgumentException(
                  optionName + " is not supported by " + type + " sockets");
            }
            if (SocketOptions.TLS_VERIFY.name().equals(optionName))
                return;
            throw new IllegalArgumentException(
              optionName + " is not supported by " + type + " sockets");
        }
        if (supportsOption(type, optionId))
            return;
        throw new IllegalArgumentException(
          optionName + " is not supported by " + type + " sockets");
    }

    private static boolean supportsOption(SocketType type, int optionId) {
        return switch (optionId) {
            case 5 -> type == SocketType.DEALER || type == SocketType.ROUTER;
            case 6, 7 -> type == SocketType.SUB || type == SocketType.XSUB;
            case 33, 51, 56, 61 -> type == SocketType.DEALER
                || type == SocketType.ROUTER;
            case 40, 69, 71, 72, 78, 0x3308, 0x3309 -> type == SocketType.PUB
                || type == SocketType.XPUB;
            case 73 -> type == SocketType.STREAM;
            case 116 -> type == SocketType.PUB || type == SocketType.XPUB
                || type == SocketType.SUB || type == SocketType.XSUB;
            default -> true;
        };
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

    void setSockOptRaw(int optionId, MemorySegment value, long len) {
        OptionRoute route = optionRoute(optionId);
        int rc = switch (route.family()) {
            case ROUTER -> Native.setRouterOption(handle, route.optionId(), value, len);
            case PUB -> Native.setPubOption(handle, route.optionId(), value, len);
            case SUB -> Native.setSubOption(handle, route.optionId(), value, len);
            case STREAM -> Native.setStreamOption(handle, route.optionId(), value, len);
            case COMMON -> Native.setSockOpt(handle,
                translateLegacyCommonOptionId(route.optionId()), value, len);
        };
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_setsockopt");
    }

    void setSockOptBytes(int optionId, byte[] value, int offset,
                         int length) {
        MemorySegment buf = length == 0 ? MemorySegment.NULL
            : ensureSendScratch(length);
        if (length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), offset, buf, 0,
                length);
        }
        setSockOptRaw(optionId, buf, length);
    }

    void setTypedBytesOption(int optionId, byte[] value, int offset,
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

    void setRoutingIdBytes(byte[] value, int offset, int length) {
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

    byte[] getRoutingIdBytes() {
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

    void setSubscriptionBytes(byte[] value, int offset, int length,
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

    void setSockOptInt(int optionId, int value) {
        MemorySegment buf = ensureSendScratch(Integer.BYTES);
        buf.set(ValueLayout.JAVA_INT, 0, value);
        setSockOptRaw(optionId, buf, Integer.BYTES);
    }

    void setSockOptLong(int optionId, long value) {
        MemorySegment buf = ensureSendScratch(Long.BYTES);
        buf.set(ValueLayout.JAVA_LONG, 0, value);
        setSockOptRaw(optionId, buf, Long.BYTES);
    }

    byte[] getSockOptBytes(int optionId, int maxLen) {
        if (maxLen < 0)
            throw new IllegalArgumentException("maxLen must be >= 0");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = maxLen == 0 ? MemorySegment.NULL
                : arena.allocate(maxLen);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, maxLen);
            OptionRoute route = optionRoute(optionId);
            int rc = switch (route.family()) {
                case ROUTER -> Native.getRouterOption(handle, route.optionId(), buf, len);
                case PUB -> Native.getPubOption(handle, route.optionId(), buf, len);
                case SUB -> Native.getSubOption(handle, route.optionId(), buf, len);
                case STREAM -> Native.getStreamOption(handle, route.optionId(), buf, len);
                case COMMON -> Native.getSockOpt(handle,
                    translateLegacyCommonOptionId(route.optionId()), buf, len);
            };
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

    int getSockOptInt(int optionId) {
        if (optionId == SocketOption.RCVMORE.getValue()) {
            return legacyReceiveState.get().pendingCount() > 0 ? 1 : 0;
        }
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, ValueLayout.JAVA_INT.byteSize());
            OptionRoute route = optionRoute(optionId);
            int rc = switch (route.family()) {
                case ROUTER -> Native.getRouterOption(handle, route.optionId(), buf, len);
                case PUB -> Native.getPubOption(handle, route.optionId(), buf, len);
                case SUB -> Native.getSubOption(handle, route.optionId(), buf, len);
                case STREAM -> Native.getStreamOption(handle, route.optionId(), buf, len);
                case COMMON -> Native.getSockOpt(handle,
                    translateLegacyCommonOptionId(route.optionId()), buf, len);
            };
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_getsockopt");
            return buf.get(ValueLayout.JAVA_INT, 0);
        }
    }

    void sendMessageFrame(Message message, SendFlag flag) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(flag, "flag");
        ensureBlockingSendAllowed(flag);
        while (true) {
            int rc = sendPartOnce(message, null, flag.getValue(),
                Native.PART_FINAL);
            if (rc == 0)
                return;
            int errno = Native.errno();
            if (errno == ERRNO_EINTR)
                continue;
            throwPartSubmitFailure("zlink_send_part");
        }
    }

    boolean sendMessageFrameNoWaitResult(Message message, SendFlag flag) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(flag, "flag");
        while (true) {
            int rc = sendPartOnce(message, null, flag.getValue(),
                Native.PART_FINAL);
            if (rc == 0)
                return true;
            int errno = Native.errno();
            if (errno == ERRNO_EINTR)
                continue;
            if (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN)
                return false;
            throw ZlinkException.fromLastError("zlink_send_part");
        }
    }

    SendResult sendMessageFrameNoWaitResult(Message message) {
        Objects.requireNonNull(message, "message");
        while (true) {
            int rc = sendPartOnce(message, null, SendFlag.DONTWAIT.getValue(),
                Native.PART_FINAL);
            if (rc == 0)
                return SendResult.SENT;
            int errno = Native.errno();
            if (errno == ERRNO_EINTR)
                continue;
            return classifyNonBlockingSendErrno("zlink_send_part");
        }
    }

    private int sendPartOnce(Message message, RoutingId routingId, int flags,
                             int partFlag) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            MemorySegment nativeRoutingId = routingId == null
                ? MemorySegment.NULL
                : nativeRoutingId(arena, routingId);
            Object anchor = message.transferTo(nativeMsg);
            try {
                int rc = nativeRoutingId.address() == 0
                    ? Native.sendPart(handle, nativeMsg, flags, partFlag)
                    : Native.sendPartRid(handle, nativeRoutingId, nativeMsg,
                        flags, partFlag);
                if (rc != 0) {
                    message.restoreFromNative(nativeMsg, false, anchor);
                }
                return rc;
            } catch (RuntimeException ex) {
                message.restoreFromNative(nativeMsg, false, anchor);
                throw ex;
            }
        }
    }

    private int publishPartOnce(String topicId, Message message, int flags,
                                int partFlag) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeTopic = arena.allocateFrom(topicId,
                StandardCharsets.UTF_8);
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            Object anchor = message.transferTo(nativeMsg);
            try {
                int rc = Native.publishPart(handle, nativeTopic, nativeMsg,
                    flags, partFlag);
                if (rc != 0) {
                    message.restoreFromNative(nativeMsg, false, anchor);
                }
                return rc;
            } catch (RuntimeException ex) {
                message.restoreFromNative(nativeMsg, false, anchor);
                throw ex;
            }
        }
    }

    void recvMessageFrame(Message message, ReceiveFlag flag) {
        Message frame = nextRecvFrame(flag, false);
        try {
            frame.moveInto(message, frame.more());
        } finally {
            frame.close();
        }
    }

    int recvMessageFrameNoWait(Message message, ReceiveFlag flag) {
        Message frame = nextRecvFrame(flag, true);
        if (frame == null)
            return -1;
        try {
            return frame.moveInto(message, frame.more());
        } finally {
            frame.close();
        }
    }

    Message nextRecvFrame(ReceiveFlag flags, boolean nonBlocking) {
        Objects.requireNonNull(flags, "flags");
        prepareRecvLikeOperation();
        LegacyReceiveState state = legacyReceiveState.get();
        while (true) {
            if (state.hasPending()) {
                return state.poll();
            }
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment sourceRidOut = arena.allocate(ValueLayout.ADDRESS);
                MemorySegment hasMoreOut = arena.allocate(ValueLayout.JAVA_INT);
                Message firstPart = new Message();
                boolean success = false;
                try {
                    int rc = Native.recvPart(handle, sourceRidOut,
                        firstPart.nativeHandle(), hasMoreOut, flags.getValue());
                    if (rc == 0) {
                        success = true;
                        boolean hasMore = hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                        firstPart.finishReceive(hasMore);
                        byte[] routingId = decodeRoutingIdPtr(
                            sourceRidOut.get(ValueLayout.ADDRESS, 0));
                        if (routingId == null || routingId.length == 0) {
                            return firstPart;
                        }
                        if (hasMore) {
                            firstPart.setMore(true);
                            state.replace(new Message[] {
                                firstPart
                            });
                        } else {
                            state.replace(new Message[] {firstPart});
                        }
                        Message routingFrame = Message.copyOf(routingId);
                        routingFrame.setMore(true);
                        return routingFrame;
                    }
                } finally {
                    if (!success) {
                        try {
                            firstPart.close();
                        } catch (RuntimeException ignored) {
                        }
                    }
                }
            }
            int errno = Native.errno();
            if (errno == ERRNO_EINTR)
                continue;
            if (nonBlocking
                && (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN)) {
                return null;
            }
            throw ZlinkException.fromLastError("zlink_recv_part");
        }
    }

    void sendParts(RoutingId routingId, List<Message> parts,
                   SendFlag flags, boolean nonBlocking) {
        ensureOpen();
        validateParts(parts);
        ensureBlockingSendAllowed(flags);
        boolean explicitNonBlocking =
            (flags.getValue() & SendFlag.DONTWAIT.getValue()) != 0;
        for (int i = 0; i < parts.size(); i++) {
            int partFlag = i + 1 < parts.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = sendPartOnce(parts.get(i), routingId, flags.getValue(),
                    partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == ERRNO_EINTR)
                    continue;
                if ((nonBlocking || explicitNonBlocking)
                    && (errno == ERRNO_EAGAIN
                        || errno == ERRNO_EWOULDBLOCK_WIN)) {
                    throw new SubmitException(SubmitResult.BACKPRESSURED,
                        errno);
                }
                throwPartSubmitFailure(
                    routingId == null ? "zlink_send_part"
                        : "zlink_send_part_rid");
            }
        }
    }

    SendResult sendNoWaitPartsResult(RoutingId routingId, List<Message> parts) {
        ensureOpen();
        validateParts(parts);
        for (int i = 0; i < parts.size(); i++) {
            int partFlag = i + 1 < parts.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = sendPartOnce(parts.get(i), routingId,
                    SendFlag.DONTWAIT.getValue(), partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == ERRNO_EINTR)
                    continue;
                return classifyNonBlockingSendErrno(
                    routingId == null ? "zlink_send_part"
                        : "zlink_send_part_rid");
            }
        }
        return SendResult.SENT;
    }

    void publishParts(String topicId, List<Message> parts,
                      SendFlag flags, boolean nonBlocking) {
        ensureOpen();
        validateParts(parts);
        ensureBlockingSendAllowed(flags);
        boolean explicitNonBlocking =
            (flags.getValue() & SendFlag.DONTWAIT.getValue()) != 0;
        for (int i = 0; i < parts.size(); i++) {
            int partFlag = i + 1 < parts.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = publishPartOnce(topicId, parts.get(i),
                    flags.getValue(), partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == ERRNO_EINTR)
                    continue;
                if ((nonBlocking || explicitNonBlocking)
                    && (errno == ERRNO_EAGAIN
                        || errno == ERRNO_EWOULDBLOCK_WIN)) {
                    throw new SubmitException(SubmitResult.BACKPRESSURED,
                        errno);
                }
                throwPartSubmitFailure("zlink_publish_part");
            }
        }
    }

    SendResult publishNoWaitPartsResult(String topicId, List<Message> parts) {
        ensureOpen();
        validateParts(parts);
        for (int i = 0; i < parts.size(); i++) {
            int partFlag = i + 1 < parts.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = publishPartOnce(topicId, parts.get(i),
                    SendFlag.DONTWAIT.getValue(), partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == ERRNO_EINTR)
                    continue;
                return classifyNonBlockingSendErrno("zlink_publish_part");
            }
        }
        return SendResult.SENT;
    }

    private SendResult classifyNonBlockingSendErrno(String apiName) {
        int errno = Native.errno();
        if (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN)
            return SendResult.BACKPRESSURED;
        if (errno == ERRNO_ENOTCONN || errno == ERRNO_ENOTCONN_WIN) {
            return SendResult.NOT_READY;
        }
        if (errno == ERRNO_EHOSTUNREACH || errno == ERRNO_EHOSTUNREACH_WIN) {
            return SendResult.NOT_READY;
        }
        if (errno == 111 || errno == 10061) {
            throw new SubmitException(SubmitResult.NOT_ADMITTED, errno);
        }
        throw ZlinkException.fromLastError(apiName);
    }

    static SubmitException submitExceptionFromSendResult(int rc) {
        return switch (SendResult.fromNativeValue(rc)) {
            case BACKPRESSURED -> new SubmitException(SubmitResult.BACKPRESSURED, 0);
            case NOT_READY -> new SubmitException(SubmitResult.NOT_CONNECTED, 0);
            case SENT -> throw new IllegalArgumentException("send result indicates success");
        };
    }

    private static boolean trySendResult(SendResult result) {
        return switch (result) {
            case SENT -> true;
            case BACKPRESSURED -> false;
            case NOT_READY -> throw submitExceptionFromSendResult(result.nativeValue());
        };
    }

    private void throwPartSubmitFailure(String apiName) {
        int errno = Native.errno();
        if (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN) {
            throw new SubmitException(SubmitResult.BACKPRESSURED, errno);
        }
        if (errno == ERRNO_ENOTCONN || errno == ERRNO_ENOTCONN_WIN
            || errno == ERRNO_EHOSTUNREACH || errno == ERRNO_EHOSTUNREACH_WIN) {
            throw new SubmitException(SubmitResult.NOT_CONNECTED, errno);
        }
        if (errno == 111 || errno == 10061) {
            throw new SubmitException(SubmitResult.NOT_ADMITTED, errno);
        }
        throw ZlinkException.fromLastError(apiName);
    }

    private static void validateParts(List<Message> parts) {
        if (parts.isEmpty())
            throw new IllegalArgumentException("parts must not be empty");
        for (int i = 0; i < parts.size(); i++) {
            if (parts.get(i) == null)
                throw new IllegalArgumentException("parts[" + i + "] is null");
        }
    }

    /**
     * Rejects blocking sends from callback context without mutating the caller's
     * requested send flags.
     */
    private static void ensureBlockingSendAllowed(SendFlag flags) {
        Objects.requireNonNull(flags, "flags");
        if (SocketCore.inCallback()
            && (flags.getValue() & SendFlag.DONTWAIT.getValue()) == 0) {
            throw new IllegalStateException(
                "blocking send is not supported from callback context; use SendFlag.DONTWAIT");
        }
    }

    static RoutingId toRoutingId(byte[] value) {
        if (value == null || value.length == 0)
            return null;
        return RoutingId.fromTrusted(value);
    }

    static byte[] decodeRoutingIdPtr(MemorySegment nativeRidPtr) {
        if (nativeRidPtr == null || nativeRidPtr.address() == 0)
            return null;
        MemorySegment routingId = nativeRidPtr.reinterpret(
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize());
        return decodeRoutingId(routingId);
    }

    static byte[] decodeRoutingId(MemorySegment nativeRid) {
        int size = nativeRid.get(ValueLayout.JAVA_BYTE,
            NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        if (size == 0)
            return null;
        byte[] value = new byte[size];
        MemorySegment.copy(nativeRid, NativeLayouts.ROUTING_ID_DATA_OFFSET,
            MemorySegment.ofArray(value), 0, size);
        return value;
    }

    static int normalizeTopicLength(MemorySegment topic, int capacity,
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

    void prepareRecvLikeOperation() {
        legacyReceiveState.get().closeRemaining();
        Received active = activeLazyReceive.get();
        if (active != null) {
            active.forceMaterialize();
            activeLazyReceive.remove();
        }
    }

    Runnable lazyReceiveCompletion(Received received) {
        return () -> {
            Received active = activeLazyReceive.get();
            if (active == received) {
                activeLazyReceive.remove();
            }
        };
    }

    Received registerLazyReceive(Received received, boolean hasMore) {
        if (hasMore) {
            activeLazyReceive.set(received);
        }
        return received;
    }

    private static MemorySegment nativeRoutingId(Arena arena, RoutingId routingId) {
        byte[] value = routingId.trustedBytes();
        MemorySegment nativeRid = arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
        nativeRid.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
            (byte) value.length);
        if (value.length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), 0, nativeRid,
                NativeLayouts.ROUTING_ID_DATA_OFFSET, value.length);
        }
        return nativeRid;
    }

    private static MemorySegment nativeRoutingId(SendScratch scratch,
                                                 RoutingId routingId) {
        byte[] value = routingId.trustedBytes();
        MemorySegment nativeRid = scratch.nativeRoutingId;
        nativeRid.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
            (byte) value.length);
        if (value.length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), 0, nativeRid,
                NativeLayouts.ROUTING_ID_DATA_OFFSET, value.length);
        }
        return nativeRid;
    }

    private static MemorySegment nativeRoutingIdU32(SendScratch scratch,
                                                    int routingId) {
        MemorySegment nativeRid = scratch.nativeRoutingId;
        nativeRid.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
            (byte) Integer.BYTES);
        nativeRid.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_DATA_OFFSET,
            (byte) (routingId >>> 24));
        nativeRid.set(ValueLayout.JAVA_BYTE,
            NativeLayouts.ROUTING_ID_DATA_OFFSET + 1, (byte) (routingId >>> 16));
        nativeRid.set(ValueLayout.JAVA_BYTE,
            NativeLayouts.ROUTING_ID_DATA_OFFSET + 2, (byte) (routingId >>> 8));
        nativeRid.set(ValueLayout.JAVA_BYTE,
            NativeLayouts.ROUTING_ID_DATA_OFFSET + 3, (byte) routingId);
        return nativeRid;
    }

    byte[] subscriptionAt(long index, MemorySegment lenInOut,
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
            OptionRoute route = optionRoute(optionId);
            int rc = switch (route.family()) {
                case ROUTER -> Native.getRouterOption(handle, route.optionId(), buf, len);
                case PUB -> Native.getPubOption(handle, route.optionId(), buf, len);
                case SUB -> Native.getSubOption(handle, route.optionId(), buf, len);
                case STREAM -> Native.getStreamOption(handle, route.optionId(), buf, len);
                case COMMON -> Native.getSockOpt(handle,
                    translateLegacyCommonOptionId(route.optionId()), buf, len);
            };
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_getsockopt");
            return buf.get(ValueLayout.JAVA_LONG, 0);
        }
    }

    private OptionRoute optionRoute(int optionId) {
        SocketType type = resolveSocketType();
        if (optionId == SocketOption.ROUTER_MANDATORY.getValue()) {
            return new OptionRoute(OptionFamily.ROUTER, 0x3101);
        }
        if (optionId == SocketOption.PROBE_ROUTER.getValue()) {
            return new OptionRoute(OptionFamily.ROUTER, 0x3103);
        }
        if (optionId == SocketOption.CONNECT_ROUTING_ID.getValue()) {
            return new OptionRoute(OptionFamily.ROUTER, 0x3104);
        }
        if (optionId == SocketOption.XPUB_VERBOSE.getValue()) {
            return new OptionRoute(OptionFamily.PUB, 0x3301);
        }
        if (optionId == SocketOption.XPUB_VERBOSER.getValue()) {
            return new OptionRoute(OptionFamily.PUB, 0x3302);
        }
        if (optionId == SocketOption.XPUB_MANUAL.getValue()) {
            return new OptionRoute(OptionFamily.PUB, 0x3303);
        }
        if (optionId == SocketOption.XPUB_MANUAL_LAST_VALUE.getValue()
            && type == SocketType.XPUB) {
            return new OptionRoute(OptionFamily.PUB, 0x3304);
        }
        if (optionId == SocketOption.XPUB_NODROP.getValue()) {
            return new OptionRoute(OptionFamily.PUB, 0x3305);
        }
        if (optionId == SocketOption.XPUB_WELCOME_MSG.getValue()) {
            return new OptionRoute(OptionFamily.PUB, 0x3306);
        }
        if (optionId == SocketOption.TOPICS_COUNT.getValue()) {
            if (type == SocketType.SUB || type == SocketType.XSUB) {
                return new OptionRoute(OptionFamily.SUB, 0x3400);
            }
            if (type == SocketType.PUB || type == SocketType.XPUB) {
                return new OptionRoute(OptionFamily.PUB, 0x3307);
            }
        }
        if (optionId == SocketOption.PUB_APPROVE_SUBSCRIBE.getValue()) {
            return new OptionRoute(OptionFamily.PUB, 0x3308);
        }
        if (optionId == SocketOption.PUB_REJECT_SUBSCRIBE.getValue()) {
            return new OptionRoute(OptionFamily.PUB, 0x3309);
        }
        if (optionId == SocketOption.STREAM_NOTIFY.getValue()) {
            return new OptionRoute(OptionFamily.STREAM, 0x3501);
        }
        return new OptionRoute(OptionFamily.COMMON, optionId);
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

    MemorySegment ensureSendScratch(int length) {
        if (length <= 0)
            return MemorySegment.NULL;
        return socketCore.ensureSendScratch(length);
    }

    private static MemorySegment nettyReadableSegment(ByteBuf buf, int index,
                                                      int length) {
        if (length <= 0 || !buf.hasMemoryAddress()) {
            return MemorySegment.NULL;
        }
        return MemorySegment.ofAddress(buf.memoryAddress() + index)
            .reinterpret(length);
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

    static int toIntLength(long length) {
        if (length > Integer.MAX_VALUE) {
            throw new IllegalArgumentException("length too large: " + length);
        }
        return (int) length;
    }

    void ensureOpen() {
        socketCore.ensureOpen();
    }

    private int sendNettyFallback(ByteBuf buf,
                                  int readerIndex,
                                  int length,
                                  int sendFlags) {
        int rc;
        if (buf.hasArray()) {
            rc = send(buf.array(),
              buf.arrayOffset() + readerIndex, length, sendFlags);
        } else {
            byte[] tmp = new byte[length];
            buf.getBytes(readerIndex, tmp);
            rc = send(tmp, 0, length, sendFlags);
        }
        if (rc > 0)
            buf.readerIndex(readerIndex + rc);
        return rc;
    }

    private boolean sendNettyFallbackNoWait(ByteBuf buf,
                                         int readerIndex,
                                         int length,
                                         int sendFlags) {
        boolean sent;
        if (buf.hasArray()) {
            sent = sendNoWaitResult(buf.array(),
              buf.arrayOffset() + readerIndex, length, sendFlags);
        } else {
            byte[] tmp = new byte[length];
            buf.getBytes(readerIndex, tmp);
            sent = sendNoWaitResult(tmp, 0, length, sendFlags);
        }
        if (sent)
            buf.readerIndex(readerIndex + length);
        return sent;
    }

    private int recvNettyFallback(ByteBuf buf,
                                  int writerIndex,
                                  int writable,
                                  ReceiveFlag flags) {
        try (Message frame = nextRecvFrame(flags, false)) {
            int rc = Math.min(writable, frame.size());
            if (rc > 0) {
                if (buf.hasArray()) {
                    MemorySegment.copy(frame.dataSegment(rc), 0,
                      MemorySegment.ofArray(buf.array()),
                      (long) buf.arrayOffset() + writerIndex, rc);
                } else {
                    ByteBuffer src = frame.dataSegment().asSlice(0, rc)
                      .asByteBuffer();
                    buf.setBytes(writerIndex, src);
                }
                buf.writerIndex(writerIndex + rc);
            }
            return rc;
        }
    }

    private int recvNettyFallbackNoWait(ByteBuf buf,
                                     int writerIndex,
                                     int writable,
                                     ReceiveFlag flags) {
        try (Message frame = nextRecvFrame(flags, true)) {
            if (frame == null)
                return -1;
            int rc = Math.min(writable, frame.size());
            if (rc > 0) {
                if (buf.hasArray()) {
                    MemorySegment.copy(frame.dataSegment(rc), 0,
                      MemorySegment.ofArray(buf.array()),
                      (long) buf.arrayOffset() + writerIndex, rc);
                } else {
                    ByteBuffer src = frame.dataSegment().asSlice(0, rc)
                      .asByteBuffer();
                    buf.setBytes(writerIndex, src);
                }
                buf.writerIndex(writerIndex + rc);
            }
            return rc;
        }
    }

}
