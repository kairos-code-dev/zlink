/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.internal.ContractAccess;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.eventing.MonitorEventType;
import systems.zlink.contracts.eventing.SocketMonitor;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.errors.ZlinkConfigException;
import systems.zlink.contracts.errors.ConfigResult;
import systems.zlink.contracts.errors.ZlinkRecvException;
import systems.zlink.runtime.nativeapi.RecvScratch;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.runtime.nativeapi.SendScratch;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.sockets.*;
import systems.zlink.contracts.messaging.SubscriptionEntry;
import systems.zlink.contracts.messaging.SubscriptionEvent;
import systems.zlink.contracts.messaging.TopicMessage;
import systems.zlink.contracts.errors.ZlinkException;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeMessage;
import systems.zlink.runtime.nativeapi.NativeSubmitErrors;
import systems.zlink.runtime.messaging.ReceivedPartCursor;
import systems.zlink.runtime.nativeapi.RequestProgressPump;
import io.netty.buffer.ByteBuf;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;

/**
 * Abstract common socket base for zlink typed socket facades.
 */
final class NativeSocketRuntime implements AutoCloseable {
    static final int ERRNO_EFSM = 156384763;
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
    private static final int OPT_RCVMORE = 13;
    private static final byte[] EMPTY_BYTES = new byte[0];

    private final SocketCore socketCore;
    private final MessagePlane messagePlane;
    private final TopicPlane topicPlane;
    private MemorySegment handle;
    private final boolean own;
    private final SocketType socketTypeHint;
    private final ThreadLocal<SendScratch> sendScratch =
      ThreadLocal.withInitial(SendScratch::new);
    private final ThreadLocal<RecvScratch> recvScratch =
      ThreadLocal.withInitial(RecvScratch::new);
    private final ThreadLocal<MultipartReceiveState> multipartReceiveState =
      ThreadLocal.withInitial(MultipartReceiveState::new);
    private final ThreadLocal<Received> activeLazyReceive =
      new ThreadLocal<>();
    private final ThreadLocal<byte[]> nettySendScratch =
      ThreadLocal.withInitial(() -> new byte[DEFAULT_IO_BUFFER_SIZE]);

    public static void ensureRegistered() {
    }

    public void disconnectRid(RoutingId peerRid) {
        socketCore.disconnectRid(peerRid);
    }

    public static boolean inCallbackContext() {
        return SocketCore.inCallback();
    }

    public static void enterCallbackContext() {
        SocketCore.enterCallback();
    }

    public static void leaveCallbackContext() {
        SocketCore.leaveCallback();
    }

    NativeSocketRuntime(Context ctx, SocketType type) {
        this.handle = Native.socket(InternalAccess.contextHandle(ctx), type.getValue());
        if (handle == null || handle.address() == 0)
            throw ZlinkException.fromLastError("zlink_socket");
        this.own = true;
        this.socketTypeHint = type;
        this.socketCore = new SocketCore(this);
        this.messagePlane = new MessagePlane(this);
        this.topicPlane = new TopicPlane(this);
    }

    NativeSocketRuntime(MemorySegment handle, boolean own, SocketType socketTypeHint) {
        this.handle = handle;
        this.own = own;
        this.socketTypeHint = socketTypeHint;
        this.socketCore = new SocketCore(this);
        this.messagePlane = new MessagePlane(this);
        this.topicPlane = new TopicPlane(this);
    }

    /** Binds the socket to the endpoint. */
    public void bind(String endpoint) {
        socketCore.bind(endpoint);
    }

    /** Connects the socket to the endpoint. */
    public void connect(String endpoint) {
        socketCore.connect(endpoint);
    }

    /** Unbinds the socket from the endpoint. */
    public void unbind(String endpoint) {
        socketCore.unbind(endpoint);
    }

    /** Disconnects the socket from the endpoint. */
    public void disconnect(String endpoint) {
        socketCore.disconnect(endpoint);
    }

    /** Attaches a fixed-service discovery view to the socket. */
    public void attachDiscovery(Discovery discovery) {
        socketCore.attachDiscovery(discovery);
    }

    public void setChannelName(String channelName) {
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

    public String getChannelName() {
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

    public void attachStreamRaw(StreamRawPacketHandler handler) {
        socketCore.attachStreamRaw(handler);
    }

    public void attachStreamRaw(StreamUInt32RawNativeHandler handler) {
        socketCore.attachStreamRaw(handler);
    }

    public void attachStreamPacket(StreamFramedPacketHandler handler) {
        socketCore.attachStreamPacket(handler);
    }

    public void attachStreamPacket(StreamUInt32FramedPacketHandler handler) {
        socketCore.attachStreamPacket(handler);
    }

    public void attachStreamPacket(StreamUInt32FramedNativeHandler handler) {
        socketCore.attachStreamPacket(handler);
    }

    public void detachStream() {
        socketCore.detachStream();
    }

    public void setSockOpt(int optionId, String optionName, byte[] value) {
        socketCore.setSockOpt(optionId, optionName, value);
    }

    public void setSockOpt(int optionId, String optionName, byte[] value,
                           int offset, int length) {
        socketCore.setSockOpt(optionId, optionName, value, offset, length);
    }

    public void setSockOpt(int optionId, String optionName, ByteBuffer value) {
        socketCore.setSockOpt(optionId, optionName, value);
    }

    public void setSockOpt(int optionId, String optionName, int value) {
        socketCore.setSockOpt(optionId, optionName, value);
    }

    public byte[] getSockOptBytes(int optionId, String optionName, int maxLen) {
        return socketCore.getSockOptBytes(optionId, optionName, maxLen);
    }

    public int getSockOptInt(int optionId, String optionName) {
        return socketCore.getSockOptInt(optionId, optionName);
    }

    public void setOption(SocketOptionKey<Integer> option, int value) {
        socketCore.setOption(option, value);
    }

    public void setOption(SocketOptionKey<Long> option, long value) {
        socketCore.setOptionLong(option, value);
    }

    public void setOption(SocketOptionKey<String> option, String value) {
        socketCore.setOptionString(option, value);
    }

    public void setOption(SocketOptionKey<byte[]> option, byte[] value) {
        socketCore.setOptionBytes(option, value);
    }

    void setDealerIntOption(int option, int value) {
        setNativeIntOption(option, value, Native::setDealerOption);
    }

    int getDealerIntOption(int option) {
        return getNativeIntOption(option, Native::getDealerOption);
    }

    int getRouterIntOption(int option) {
        return getNativeIntOption(option, Native::getRouterOption);
    }

    void setRouterIntOption(int option, int value) {
        setNativeIntOption(option, value, Native::setRouterOption);
    }

    private int getNativeIntOption(int option, NativeIntOptionGetter getter) {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeValue = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, ValueLayout.JAVA_INT.byteSize());
            int rc = getter.get(handle, option, nativeValue, len);
            if (rc != 0) {
                throw new ZlinkConfigException(ConfigResult.fromValue(rc));
            }
            return nativeValue.get(ValueLayout.JAVA_INT, 0);
        }
    }

    private void setNativeIntOption(int option, int value,
                                    NativeIntOptionSetter setter) {
        ensureOpen();
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeValue = arena.allocate(ValueLayout.JAVA_INT);
            nativeValue.set(ValueLayout.JAVA_INT, 0, value);
            int rc = setter.set(handle, option, nativeValue,
              ValueLayout.JAVA_INT.byteSize());
            if (rc != 0) {
                throw new ZlinkConfigException(ConfigResult.fromValue(rc));
            }
        }
    }

    @FunctionalInterface
    private interface NativeIntOptionGetter {
        int get(MemorySegment handle, int option, MemorySegment value,
                MemorySegment len);
    }

    @FunctionalInterface
    private interface NativeIntOptionSetter {
        int set(MemorySegment handle, int option, MemorySegment value,
                long len);
    }

    @SuppressWarnings("unchecked")
    public <T> T getOption(SocketOptionKey<T> option) {
        return socketCore.getOption(option);
    }

    /** Opens a socket monitor for all events. */
    public SocketMonitor monitorOpen() {
        return monitorOpen(MonitorEventType.ALL);
    }

    /** Opens a socket monitor for the requested event types. */
    public SocketMonitor monitorOpen(MonitorEventType... events) {
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

    public boolean send(Message part) {
        messagePlane.send(part);
        return true;
    }

    public boolean send(Message part, SendFlag flags) {
        Objects.requireNonNull(flags, "flags");
        if ((flags.getValue() & SendFlag.DONTWAIT.getValue()) != 0) {
            return trySendResult(sendNoWaitResult(part));
        }
        messagePlane.send(part, flags);
        return true;
    }

    public void sendMessageFrame(RoutingId routingId, Message message, SendFlag flag) {
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
            if (isTransientBlockingSendErrno(errno))
                continue;
            throwPartSubmitFailure("zlink_send_part_rid");
        }
    }

    private static boolean isTransientBlockingSendErrno(int errno) {
        return errno == ERRNO_EINTR
            || errno == ERRNO_EAGAIN
            || errno == ERRNO_EWOULDBLOCK_WIN
            || errno == ERRNO_ENOTCONN
            || errno == ERRNO_ENOTCONN_WIN
            || errno == ERRNO_EHOSTUNREACH
            || errno == ERRNO_EHOSTUNREACH_WIN;
    }

    public boolean send(List<Message> parts) {
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

    boolean send(byte[] routingIdBytes, Message part, SendFlag flags) {
        Objects.requireNonNull(routingIdBytes, "routingIdBytes");
        Objects.requireNonNull(part, "part");
        Objects.requireNonNull(flags, "flags");
        ensureBlockingSendAllowed(flags);
        while (true) {
            int rc = sendPartOnce(part, routingIdBytes, flags.getValue(),
                Native.PART_FINAL);
            if (rc == 0)
                return true;
            int errno = Native.errno();
            if (errno == ERRNO_EINTR)
                continue;
            if ((flags.getValue() & SendFlag.DONTWAIT.getValue()) != 0
                && (errno == ERRNO_EAGAIN
                    || errno == ERRNO_EWOULDBLOCK_WIN)) {
                return false;
            }
            throwPartSubmitFailure("zlink_send_part_rid");
        }
    }

    void send(int rid, Message part, SendFlag flags) {
        Objects.requireNonNull(part, "part");
        Objects.requireNonNull(flags, "flags");
        ensureBlockingSendAllowed(flags);
        int effectiveFlags = flags.getValue();
        int rc = Native.sendMultipartU32(handle, rid, InternalAccess.messageNativeHandle(part), 1,
            effectiveFlags);
        if (rc < 0)
            throw ZlinkException.fromLastError("zlink_java_send_u32");
        InternalAccess.messageMarkTransferred(part);
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
        int rc = NativeMessage.messageInitSize(nativeMsg, length);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_msg_init_size");
        if (length > 0) {
            MemorySegment dst = NativeMessage.messageData(nativeMsg).reinterpret(length);
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
                    NativeMessage.messageClose(nativeMsg);
                } catch (RuntimeException ignored) {
                }
            }
        }
        return length;
    }

    private int sendDirectSegment(int rid, MemorySegment payload, int length,
                                  SendFlag flag) {
        return sendCopied(rid, payload, length, flag.getValue());
    }

    public boolean send(RoutingId rid, List<Message> parts) {
        messagePlane.send(rid, parts);
        return true;
    }

    public boolean send(RoutingId rid, List<Message> parts, SendFlag flags) {
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
    public boolean publish(String topicId, Message part) {
        topicPlane.publish(topicId, part);
        return true;
    }

    /** Publishes a single payload part with explicit send flags. */
    public boolean publish(String topicId, Message part, SendFlag flags) {
        Objects.requireNonNull(flags, "flags");
        if ((flags.getValue() & SendFlag.DONTWAIT.getValue()) != 0) {
            return trySendResult(publishNoWaitResult(topicId, part));
        }
        topicPlane.publish(topicId, part, flags);
        return true;
    }

    public void publishMessageFrame(String topicId, Message message, SendFlag flags) {
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
    public boolean publish(String topicId, List<Message> parts) {
        topicPlane.publish(topicId, parts);
        return true;
    }

    /** Publishes a multipart payload with explicit send flags. */
    public boolean publish(String topicId, List<Message> parts, SendFlag flags) {
        Objects.requireNonNull(flags, "flags");
        if ((flags.getValue() & SendFlag.DONTWAIT.getValue()) != 0) {
            return trySendResult(publishNoWaitResult(topicId, parts));
        }
        topicPlane.publish(topicId, parts, flags);
        return true;
    }

    public SendResult publishNoWaitResult(String topicId, Message part) {
        return topicPlane.publishNoWaitResult(topicId, part);
    }

    public SendResult publishMessageFrameNoWaitResult(String topicId, Message message) {
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

    public SendResult publishNoWaitResult(String topicId, List<Message> parts) {
        return topicPlane.publishNoWaitResult(topicId, parts);
    }

    public Received recv() {
        return messagePlane.recv();
    }

    public Received recv(ReceiveFlag flags) {
        Objects.requireNonNull(flags, "flags");
        if (flags == ReceiveFlag.DONTWAIT) {
            return messagePlane.recvNoWaitOrNull();
        }
        return messagePlane.recv(flags);
    }

    public Received recvNoWaitOrNull() {
        return messagePlane.recvNoWaitOrNull();
    }

    public boolean recvInto(Received result, ReceiveFlag flags) {
        Objects.requireNonNull(result, "result");
        Objects.requireNonNull(flags, "flags");
        if (flags == ReceiveFlag.DONTWAIT
            && !multipartReceiveState.get().hasPending()) {
            return recvIntoNoWait(result);
        }
        Message frame = nextRecvFrame(flags, flags == ReceiveFlag.DONTWAIT);
        if (frame == null) {
            return false;
        }
        if (!frame.more()) {
            ContractAccess.receivedPopulateRoutedSinglePart(result, null, null,
                frame, 0L, false, null, null);
            return true;
        }

        Received fresh = InternalAccess.receivedLazy((byte[]) null, null, frame,
            new BasicReceiveCursor(flags.getValue()), 0L, false, null, null);
        result.adoptFrom(fresh);
        return true;
    }

    private boolean recvIntoNoWait(Received result) {
        prepareRecvLikeOperation();
        RecvScratch scratch = recvScratch.get();
        while (true) {
            Message frame = new Message();
            boolean success = false;
            try {
                int rc = Native.recvPartNoWaitCritical(handle,
                    scratch.sourceRidOut, InternalAccess.messageNativeHandle(frame),
                    scratch.hasMoreOut, ReceiveFlag.DONTWAIT.getValue());
                if (rc == 0) {
                    success = true;
                    boolean hasMore =
                        scratch.hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                    InternalAccess.messageFinishReceive(frame, hasMore);
                    if (!hasMore) {
                        ContractAccess.receivedPopulateRoutedSinglePart(result,
                            null, null, frame, 0L, false, null, null);
                    } else {
                        Received fresh = InternalAccess.receivedLazy((byte[]) null, null,
                            frame,
                            new BasicReceiveCursor(
                                ReceiveFlag.DONTWAIT.getValue()),
                            0L, false, null, null);
                        result.adoptFrom(fresh);
                    }
                    return true;
                }
            } finally {
                if (!success) {
                    try {
                        frame.close();
                    } catch (RuntimeException ignored) {
                    }
                }
            }

            int errno = Native.errno();
            if (errno == ERRNO_EINTR) {
                continue;
            }
            if (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN) {
                return false;
            }
            throw ZlinkException.fromLastError("zlink_recv_part");
        }
    }

    public Optional<Received> recvNoWait() {
        return Optional.ofNullable(recvNoWaitOrNull());
    }

    public Received recvLazy(ReceiveFlag flags) {
        Received received = recvLazyOrNull(flags, false);
        if (received == null) {
            throw new ZlinkRecvException(RecvResult.NO_DATA, ERRNO_EAGAIN);
        }
        return received;
    }

    public Received recvLazyNoWaitOrNull() {
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
                    int rc = Native.recv(handle, sourceRidOut,
                        InternalAccess.messageNativeHandle(firstPart), hasMoreOut, flags.getValue());
                    if (rc == 0) {
                        success = true;
                        boolean hasMore =
                            hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                        InternalAccess.messageFinishReceive(firstPart, hasMore);
                        byte[] routingId = decodeRoutingIdPtr(
                            sourceRidOut.get(ValueLayout.ADDRESS, 0));
                        ReceivedPartCursor cursor = hasMore
                            ? new BasicReceiveCursor(flags.getValue())
                            : null;
                        Received[] ref = new Received[1];
                        Runnable onTerminal = () -> {
                            Received active = activeLazyReceive.get();
                            if (active == ref[0]) {
                                activeLazyReceive.remove();
                            }
                        };
                        Received received = InternalAccess.receivedLazy(routingId, null, firstPart, cursor, 0L, false, null, onTerminal);
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
    public TopicMessage subscribe() {
        return topicPlane.subscribe();
    }

    /** Receives a topic-aware delivery with explicit receive flags. */
    public TopicMessage subscribe(ReceiveFlag flags) {
        Objects.requireNonNull(flags, "flags");
        if (flags == ReceiveFlag.DONTWAIT) {
            return topicPlane.subscribeNoWait().orElse(null);
        }
        return topicPlane.subscribe(flags);
    }

    public boolean subscribe(TopicMessage result, ReceiveFlag flags) {
        Objects.requireNonNull(result, "result");
        if (flags == ReceiveFlag.DONTWAIT) {
            return subscribeIntoFastNoWait(result);
        }
        TopicMessage fresh = subscribe(flags);
        if (fresh == null)
            return false;
        result.adoptFrom(fresh);
        return true;
    }

    // Non-allocating subscribe hot path for the DONT_WAIT single-part case
    // (the perf/streaming common path). Mirrors recvIntoNoWait: thread-local
    // RecvScratch (no per-call Arena), critical downcall, cached topic
    // String, and a reused result. Multipart payloads fall back to the
    // general allocating path.
    private boolean subscribeIntoFastNoWait(TopicMessage result) {
        ensureOpen();
        prepareRecvLikeOperation();
        RecvScratch scratch = recvScratch.get();
        while (true) {
            scratch.topicLenOut.set(ValueLayout.JAVA_LONG, 0,
                RecvScratch.TOPIC_CAPACITY);
            Message part = new Message();
            boolean success = false;
            try {
                int rc = Native.subscribePartNoWaitCritical(handle,
                    scratch.sourceRidOut, scratch.topicOut,
                    RecvScratch.TOPIC_CAPACITY, scratch.topicLenOut,
                    InternalAccess.messageNativeHandle(part), scratch.hasMoreOut,
                    ReceiveFlag.DONTWAIT.getValue());
                if (rc == 0) {
                    boolean hasMore =
                        scratch.hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                    InternalAccess.messageFinishReceive(part, hasMore);
                    if (hasMore) {
                        // Rare multipart payload: hand the already-received
                        // first part to the general path to assemble the
                        // remainder, preserving exact semantics.
                        success = true;
                        TopicMessage fresh = subscribeAssembleRemainder(
                            scratch, part);
                        result.adoptFrom(fresh);
                        return true;
                    }
                    RoutingId routingId = NativeSocketRuntime.toRoutingId(
                        NativeSocketRuntime.decodeRoutingIdPtr(scratch.sourceRidOut.get(
                            ValueLayout.ADDRESS, 0)));
                    int topicLength = NativeSocketRuntime.normalizeTopicLength(
                        scratch.topicOut, RecvScratch.TOPIC_CAPACITY,
                        scratch.topicLenOut.get(ValueLayout.JAVA_LONG, 0));
                    String topicId = cachedTopicString(scratch,
                        scratch.topicOut, topicLength);
                    success = true;
                    InternalAccess.topicMessageAdoptSingle(result, routingId, topicId, part);
                    return true;
                }
            } finally {
                if (!success) {
                    try {
                        part.close();
                    } catch (RuntimeException ignored) {
                    }
                }
            }

            int errno = Native.errno();
            if (errno == ERRNO_EINTR) {
                continue;
            }
            if (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN) {
                return false;
            }
            throw ZlinkException.fromLastError("zlink_subscribe_part");
        }
    }

    // Decodes the topic id, reusing the last String when the raw topic bytes
    // are unchanged (steady-state single-topic streams). Mirrors C reusing a
    // fixed topic buffer and comparing against the expected constant.
    private static String cachedTopicString(RecvScratch scratch,
                                            MemorySegment topicOut,
                                            int topicLength) {
        if (topicLength == 0) {
            return "";
        }
        byte[] cached = scratch.cachedTopicBytes;
        if (cached != null && cached.length == topicLength) {
            boolean same = true;
            for (int i = 0; i < topicLength; i++) {
                if (cached[i] != topicOut.get(ValueLayout.JAVA_BYTE, i)) {
                    same = false;
                    break;
                }
            }
            if (same) {
                return scratch.cachedTopicString;
            }
        }
        byte[] raw = topicOut.asSlice(0, topicLength)
            .toArray(ValueLayout.JAVA_BYTE);
        String decoded = new String(raw, StandardCharsets.UTF_8);
        scratch.cachedTopicBytes = raw;
        scratch.cachedTopicString = decoded;
        return decoded;
    }

    // Assembles a multipart subscribe payload whose first part has already
    // been received into the fast path. Delegates the tail to the general
    // allocating reader for exact parity.
    private TopicMessage subscribeAssembleRemainder(RecvScratch scratch,
                                                    Message firstPart) {
        RoutingId routingId = NativeSocketRuntime.toRoutingId(
            NativeSocketRuntime.decodeRoutingIdPtr(scratch.sourceRidOut.get(
                ValueLayout.ADDRESS, 0)));
        int topicLength = NativeSocketRuntime.normalizeTopicLength(
            scratch.topicOut, RecvScratch.TOPIC_CAPACITY,
            scratch.topicLenOut.get(ValueLayout.JAVA_LONG, 0));
        String topicId = topicLength == 0 ? ""
            : new String(scratch.topicOut.asSlice(0, topicLength)
                .toArray(ValueLayout.JAVA_BYTE), StandardCharsets.UTF_8);
        java.util.ArrayList<Message> parts = new java.util.ArrayList<>();
        parts.add(firstPart);
        while (true) {
            Message next = new Message();
            boolean ok = false;
            try {
                int rc = Native.subscribePart(handle, scratch.sourceRidOut,
                    scratch.topicOut, RecvScratch.TOPIC_CAPACITY,
                    scratch.topicLenOut, InternalAccess.messageNativeHandle(next),
                    scratch.hasMoreOut, ReceiveFlag.NONE.getValue());
                if (rc == 0) {
                    ok = true;
                    boolean more =
                        scratch.hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                    InternalAccess.messageFinishReceive(next, more);
                    parts.add(next);
                    if (!more) {
                        return new TopicMessage(routingId, topicId,
                            parts.toArray(Message[]::new));
                    }
                    continue;
                }
            } finally {
                if (!ok) {
                    try {
                        next.close();
                    } catch (RuntimeException ignored) {
                    }
                }
            }
            int errno = Native.errno();
            if (errno == ERRNO_EINTR) {
                continue;
            }
            Message.closeAll(parts);
            throw ZlinkException.fromLastError("zlink_subscribe_part");
        }
    }

    public Optional<TopicMessage> subscribeNoWait() {
        return topicPlane.subscribeNoWait();
    }

    public SubscriptionEvent receiveSubscriptionEvent() {
        return topicPlane.subscriptionEvent(ReceiveFlag.NONE);
    }

    public SubscriptionEvent receiveSubscriptionEvent(ReceiveFlag flags) {
        return topicPlane.subscriptionEvent(flags);
    }

    public boolean receiveSubscriptionEvent(SubscriptionEvent result,
                                     ReceiveFlag flags) {
        Objects.requireNonNull(result, "result");
        SubscriptionEvent fresh;
        try {
            fresh = receiveSubscriptionEvent(flags);
        } catch (ZlinkRecvException ex) {
            if (flags == ReceiveFlag.DONTWAIT
                && ex.getResult() == RecvResult.NO_DATA) {
                return false;
            }
            throw ex;
        }
        if (fresh == null)
            return false;
        result.adoptFrom(fresh);
        return true;
    }

    public SubscriptionEvent subscriptionEvent(ReceiveFlag flags) {
        return topicPlane.subscriptionEvent(flags);
    }

    public Optional<SubscriptionEvent> tryReceiveSubscriptionEvent() {
        return topicPlane.trySubscriptionEvent();
    }

    public void setRoutingId(RoutingId rid) {
        topicPlane.setRoutingId(rid);
    }

    public RoutingId getRoutingId() {
        return topicPlane.getRoutingId();
    }

    public void setSubscription(String filter) {
        topicPlane.setSubscription(filter);
    }

    public void setSubscription(byte[] filter) {
        topicPlane.setSubscription(filter);
    }

    public void unsetSubscription(String filter) {
        topicPlane.unsetSubscription(filter);
    }

    public void unsetSubscription(byte[] filter) {
        topicPlane.unsetSubscription(filter);
    }

    public List<SubscriptionEntry> subscriptions() {
        return topicPlane.subscriptions();
    }

    public void onReceive(SocketMessageHandler handler) {
        socketCore.onReceive(handler);
    }

    public void onSubscribe(SubscribeHandler handler) {
        socketCore.onSubscribe(handler);
    }

    public void setSendReadyHandler(SendReadyHandler handler) {
        socketCore.setSendReadyHandler(handler);
    }

    private final class BasicReceiveCursor implements ReceivedPartCursor {
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
                    int rc = Native.recv(handle, sourceRidOut,
                        InternalAccess.messageNativeHandle(next), hasMoreOut, flags);
                    if (rc == 0) {
                        success = true;
                        hasMore = hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                        InternalAccess.messageFinishReceive(next, hasMore);
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
        try (Message msg = Message.from(data, offset, length)) {
            sendMessageFrame(msg, SendFlag.fromValue(sendFlags));
            return length;
        }
    }

    boolean sendNoWaitResult(byte[] data, int offset, int length, int sendFlags) {
        Objects.requireNonNull(data, "data");
        validateRange(data.length, offset, length, "data");
        try (Message msg = Message.from(data, offset, length)) {
            return sendMessageFrameNoWaitResult(msg, SendFlag.fromValue(sendFlags));
        }
    }

    int send(MemorySegment segment, long offset, long length,
             int sendFlags) {
        Objects.requireNonNull(segment, "segment");
        validateRange(segment.byteSize(), offset, length, "segment");
        try (Message msg = InternalAccess.messageFromSegment(segment, offset,
                 length)) {
            sendMessageFrame(msg, SendFlag.fromValue(sendFlags));
            return (int) length;
        }
    }

    boolean sendNoWaitResult(MemorySegment segment, long offset, long length,
                    int sendFlags) {
        Objects.requireNonNull(segment, "segment");
        validateRange(segment.byteSize(), offset, length, "segment");
        try (Message msg = InternalAccess.messageFromSegment(segment, offset,
                 length)) {
            return sendMessageFrameNoWaitResult(msg, SendFlag.fromValue(sendFlags));
        }
    }

    int send(ByteBuf buf, int sendFlags) {
        Objects.requireNonNull(buf, "buf");
        int len = buf.readableBytes();
        if (len <= 0) {
            try (Message msg = Message.from(EMPTY_BYTES)) {
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
            try (Message msg = Message.from(EMPTY_BYTES)) {
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
                MemorySegment.copy(InternalAccess.messageDataSegment(frame), 0, segment, offset, rc);
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
                MemorySegment.copy(InternalAccess.messageDataSegment(frame), 0, segment, offset, rc);
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
                ERRNO_EFSM);
        }
        if (handle != null && handle.address() != 0) {
            RequestProgressPump.stopSocketProgress(handle);
            RequestProgressPump.stopSpotProgress(handle);
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
    public <T> T readOption(SocketOptionKey<T> option) {
        return switch (option.valueType()) {
            case INT32 -> (T) Integer.valueOf(getSockOptInt(option.optionId()));
            case INT64 -> (T) Long.valueOf(getSockOptLong(option.optionId()));
            case STRING -> (T) getTypedStringOption(option);
            case BYTES -> (T) getTypedBytesOption(option);
        };
    }

    public int recvByteBufDirect(ByteBuf buf, ReceiveFlag flags) {
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

    public int recvByteBufDirectNoWait(ByteBuf buf, ReceiveFlag flags) {
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
        if (option.optionId() != SocketOptions.TLS_VERIFY.optionId())
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
        if (optionId == SocketOptions.TLS_VERIFY.optionId()) {
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
            return SocketType.fromValue(getSockOptInt(SocketOptions.TYPE.optionId()));
        } catch (RuntimeException ignored) {
            return null;
        }
    }

    void setSockOptRaw(int optionId, MemorySegment value, long len) {
        var route = optionRoute(optionId);
        int rc = switch (route.family()) {
            case ROUTER -> Native.setRouterOption(handle, route.optionId(), value, len);
            case PUB -> Native.setPubOption(handle, route.optionId(), value, len);
            case SUB -> Native.setSubOption(handle, route.optionId(), value, len);
            case STREAM -> Native.setStreamOption(handle, route.optionId(), value, len);
            case COMMON -> Native.setSockOpt(handle,
                route.nativeCommonOptionId(), value, len);
        };
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_setsockopt");
    }

    public void setSockOptBytes(int optionId, byte[] value, int offset,
                         int length) {
        MemorySegment buf = length == 0 ? MemorySegment.NULL
            : ensureSendScratch(length);
        if (length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), offset, buf, 0,
                length);
        }
        setSockOptRaw(optionId, buf, length);
    }

    public void setTypedBytesOption(int optionId, byte[] value, int offset,
                             int length) {
        if (optionId == SocketOptions.ROUTING_ID.optionId()) {
            setRoutingIdBytes(value, offset, length);
            return;
        }
        if (optionId == 6) {
            setSubscriptionBytes(value, offset, length, true);
            return;
        }
        if (optionId == 7) {
            setSubscriptionBytes(value, offset, length, false);
            return;
        }
        setSockOptBytes(optionId, value, offset, length);
    }

    @SuppressWarnings("unchecked")
    private <T> T getTypedStringOption(SocketOptionKey<T> option) {
        if (option.optionId() == SocketOptions.ROUTING_ID.optionId()) {
            return (T) new String(getRoutingIdBytes(), StandardCharsets.UTF_8);
        }
        return (T) decodeCString(
          getSockOptBytes(option.optionId(), option.maxReadLength()));
    }

    @SuppressWarnings("unchecked")
    private <T> T getTypedBytesOption(SocketOptionKey<T> option) {
        if (option.optionId() == SocketOptions.ROUTING_ID.optionId()) {
            return (T) getRoutingIdBytes();
        }
        return (T) getSockOptBytes(option.optionId(), option.maxReadLength());
    }

    public void setRoutingIdBytes(byte[] value, int offset, int length) {
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

    public byte[] getRoutingIdBytes() {
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

    public void setSubscriptionBytes(byte[] value, int offset, int length,
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

    public void setSockOptInt(int optionId, int value) {
        MemorySegment buf = ensureSendScratch(Integer.BYTES);
        buf.set(ValueLayout.JAVA_INT, 0, value);
        setSockOptRaw(optionId, buf, Integer.BYTES);
    }

    public void setSockOptLong(int optionId, long value) {
        MemorySegment buf = ensureSendScratch(Long.BYTES);
        buf.set(ValueLayout.JAVA_LONG, 0, value);
        setSockOptRaw(optionId, buf, Long.BYTES);
    }

    public byte[] getSockOptBytes(int optionId, int maxLen) {
        if (maxLen < 0)
            throw new IllegalArgumentException("maxLen must be >= 0");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = maxLen == 0 ? MemorySegment.NULL
                : arena.allocate(maxLen);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, maxLen);
            var route = optionRoute(optionId);
            int rc = switch (route.family()) {
                case ROUTER -> Native.getRouterOption(handle, route.optionId(), buf, len);
                case PUB -> Native.getPubOption(handle, route.optionId(), buf, len);
                case SUB -> Native.getSubOption(handle, route.optionId(), buf, len);
                case STREAM -> Native.getStreamOption(handle, route.optionId(), buf, len);
                case COMMON -> Native.getSockOpt(handle,
                    route.nativeCommonOptionId(), buf, len);
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

    public int getSockOptInt(int optionId) {
        if (optionId == OPT_RCVMORE) {
            return multipartReceiveState.get().pendingCount() > 0 ? 1 : 0;
        }
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, ValueLayout.JAVA_INT.byteSize());
            var route = optionRoute(optionId);
            int rc = switch (route.family()) {
                case ROUTER -> Native.getRouterOption(handle, route.optionId(), buf, len);
                case PUB -> Native.getPubOption(handle, route.optionId(), buf, len);
                case SUB -> Native.getSubOption(handle, route.optionId(), buf, len);
                case STREAM -> Native.getStreamOption(handle, route.optionId(), buf, len);
                case COMMON -> Native.getSockOpt(handle,
                    route.nativeCommonOptionId(), buf, len);
            };
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_getsockopt");
            return buf.get(ValueLayout.JAVA_INT, 0);
        }
    }

    public void sendMessageFrame(Message message, SendFlag flag) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(flag, "flag");
        ensureBlockingSendAllowed(flag);
        while (true) {
            int rc = sendPartOnce(message, (RoutingId) null, flag.getValue(),
                Native.PART_FINAL);
            if (rc == 0)
                return;
            int errno = Native.errno();
            if (isTransientBlockingSendErrno(errno))
                continue;
            throwPartSubmitFailure("zlink_send_part");
        }
    }

    public boolean sendMessageFrameNoWaitResult(Message message, SendFlag flag) {
        Objects.requireNonNull(message, "message");
        Objects.requireNonNull(flag, "flag");
        while (true) {
            int rc = sendPartOnce(message, (RoutingId) null, flag.getValue(),
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

    public SendResult sendMessageFrameNoWaitResult(Message message) {
        Objects.requireNonNull(message, "message");
        while (true) {
            int rc = sendPartOnce(message, (RoutingId) null,
                SendFlag.DONTWAIT.getValue(), Native.PART_FINAL);
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
        SendScratch scratch = sendScratch.get();
        MemorySegment messageHandle = InternalAccess.messageNativeHandle(message);
        MemorySegment nativeRoutingId = routingId == null
            ? MemorySegment.NULL
            : nativeRoutingId(scratch, routingId);
        // DONT_WAIT is contractually non-blocking — use the critical FFM
        // variant so the JVM elides GC safepoint transitions. Other flags
        // (blocking send) keep the regular handle for safety.
        boolean useCritical =
            (flags & SendFlag.DONTWAIT.getValue()) != 0;
        int rc;
        if (nativeRoutingId.address() == 0) {
            rc = useCritical
                ? Native.sendPartNoWaitCritical(handle, messageHandle, flags,
                    partFlag)
                : Native.sendPart(handle, messageHandle, flags, partFlag);
        } else {
            rc = useCritical
                ? Native.sendPartRidNoWaitCritical(handle, nativeRoutingId,
                    messageHandle, flags, partFlag)
                : Native.sendPartRid(handle, nativeRoutingId, messageHandle,
                    flags, partFlag);
        }
        if (rc == 0) {
            InternalAccess.messageMarkTransferred(message);
        }
        return rc;
    }

    private int sendPartOnce(Message message, byte[] routingIdBytes, int flags,
                             int partFlag) {
        SendScratch scratch = sendScratch.get();
        MemorySegment messageHandle = InternalAccess.messageNativeHandle(message);
        MemorySegment nativeRoutingId = nativeRoutingId(scratch,
            routingIdBytes);
        boolean useCritical =
            (flags & SendFlag.DONTWAIT.getValue()) != 0;
        int rc = useCritical
            ? Native.sendPartRidNoWaitCritical(handle, nativeRoutingId,
                messageHandle, flags, partFlag)
            : Native.sendPartRid(handle, nativeRoutingId, messageHandle,
                flags, partFlag);
        if (rc == 0) {
            InternalAccess.messageMarkTransferred(message);
        }
        return rc;
    }

    // Publish hot path. Mirrors sendPartOnce: reuse the thread-local
    // SendScratch (no per-call Arena), cache the encoded topic segment (C
    // passes a const char* with zero per-call allocation), pass the message's
    // own native handle directly (no extra messageInit/messageMove), and use the
    // safepoint-eliding critical downcall when DONT_WAIT is set.
    private int publishPartOnce(String topicId, Message message, int flags,
                                int partFlag) {
        SendScratch scratch = sendScratch.get();
        MemorySegment nativeTopic = nativeTopic(scratch, topicId);
        MemorySegment messageHandle = InternalAccess.messageNativeHandle(message);
        boolean useCritical =
            (flags & SendFlag.DONTWAIT.getValue()) != 0;
        int rc = useCritical
            ? Native.publishPartNoWaitCritical(handle, nativeTopic,
                messageHandle, flags, partFlag)
            : Native.publishPart(handle, nativeTopic, messageHandle, flags,
                partFlag);
        if (rc == 0) {
            InternalAccess.messageMarkTransferred(message);
        }
        return rc;
    }

    // Multipart publish step against a pre-encoded native topic segment.
    // Same handle-direct, critical-when-DONT_WAIT model as the single-part
    // publishPartOnce above.
    private int publishPartOnce(MemorySegment nativeTopic, Message message,
                                int flags, int partFlag) {
        MemorySegment messageHandle = InternalAccess.messageNativeHandle(message);
        boolean useCritical =
            (flags & SendFlag.DONTWAIT.getValue()) != 0;
        int rc = useCritical
            ? Native.publishPartNoWaitCritical(handle, nativeTopic,
                messageHandle, flags, partFlag)
            : Native.publishPart(handle, nativeTopic, messageHandle, flags,
                partFlag);
        if (rc == 0) {
            InternalAccess.messageMarkTransferred(message);
        }
        return rc;
    }

    // Returns a cached native UTF-8 encoding of the topic. Steady-state
    // publishes reuse the same constant topic, so the common path is a
    // reference/equality hit with no allocation or re-encoding.
    private static MemorySegment nativeTopic(SendScratch scratch,
                                             String topicId) {
        if (scratch.cachedTopicString != null
            && scratch.cachedTopicString.equals(topicId)
            && scratch.cachedTopicSegment != null) {
            return scratch.cachedTopicSegment;
        }
        MemorySegment encoded = scratch.arena.allocateFrom(topicId,
            StandardCharsets.UTF_8);
        scratch.cachedTopicString = topicId;
        scratch.cachedTopicSegment = encoded;
        return encoded;
    }

    public void recvMessageFrame(Message message, ReceiveFlag flag) {
        Message frame = nextRecvFrame(flag, false);
        try {
            InternalAccess.messageMoveInto(frame, message, frame.more());
        } finally {
            frame.close();
        }
    }

    public int recvMessageFrameNoWait(Message message, ReceiveFlag flag) {
        Message frame = nextRecvFrame(flag, true);
        if (frame == null)
            return -1;
        try {
            return InternalAccess.messageMoveInto(frame, message, frame.more());
        } finally {
            frame.close();
        }
    }

    Message nextRecvFrame(ReceiveFlag flags, boolean nonBlocking) {
        Objects.requireNonNull(flags, "flags");
        prepareRecvLikeOperation();
        MultipartReceiveState state = multipartReceiveState.get();
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
                    int rc = Native.recv(handle, sourceRidOut,
                        InternalAccess.messageNativeHandle(firstPart), hasMoreOut, flags.getValue());
                    if (rc == 0) {
                        success = true;
                        boolean hasMore = hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                        InternalAccess.messageFinishReceive(firstPart, hasMore);
                        byte[] routingId = decodeRoutingIdPtr(
                            sourceRidOut.get(ValueLayout.ADDRESS, 0));
                        if (routingId == null || routingId.length == 0) {
                            return firstPart;
                        }
                        if (hasMore) {
                            InternalAccess.messageSetMore(firstPart, true);
                            state.replace(new Message[] {
                                firstPart
                            });
                        } else {
                            state.replace(new Message[] {firstPart});
                        }
                        Message routingFrame = Message.from(routingId);
                        InternalAccess.messageSetMore(routingFrame, true);
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

    public void sendParts(RoutingId routingId, List<Message> parts,
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
                    throw new ZlinkSubmitException(SubmitResult.BACKPRESSURED,
                        errno);
                }
                throwPartSubmitFailure(
                    routingId == null ? "zlink_send_part"
                        : "zlink_send_part_rid");
            }
        }
    }

    public SendResult sendNoWaitPartsResult(RoutingId routingId, List<Message> parts) {
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

    public void publishParts(String topicId, List<Message> parts,
                      SendFlag flags, boolean nonBlocking) {
        ensureOpen();
        validateParts(parts);
        ensureBlockingSendAllowed(flags);
        boolean explicitNonBlocking =
            (flags.getValue() & SendFlag.DONTWAIT.getValue()) != 0;
        MemorySegment nativeTopic = nativeTopic(sendScratch.get(), topicId);
        for (int i = 0; i < parts.size(); i++) {
            int partFlag = i + 1 < parts.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = publishPartOnce(nativeTopic, parts.get(i),
                    flags.getValue(), partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == ERRNO_EINTR)
                    continue;
                if ((nonBlocking || explicitNonBlocking)
                    && (errno == ERRNO_EAGAIN
                        || errno == ERRNO_EWOULDBLOCK_WIN)) {
                    throw new ZlinkSubmitException(SubmitResult.BACKPRESSURED,
                        errno);
                }
                throwPartSubmitFailure("zlink_publish_part");
            }
        }
    }

    public SendResult publishNoWaitPartsResult(String topicId, List<Message> parts) {
        ensureOpen();
        validateParts(parts);
        MemorySegment nativeTopic = nativeTopic(sendScratch.get(), topicId);
        for (int i = 0; i < parts.size(); i++) {
            int partFlag = i + 1 < parts.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = publishPartOnce(nativeTopic, parts.get(i),
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
        if (NativeSubmitErrors.isBackpressured(errno))
            return SendResult.BACKPRESSURED;
        if (NativeSubmitErrors.isNotConnected(errno))
            return SendResult.NOT_READY;
        if (NativeSubmitErrors.isNotAdmitted(errno)) {
            throw new ZlinkSubmitException(SubmitResult.NOT_ADMITTED, errno);
        }
        throw ZlinkException.fromLastError(apiName);
    }

    static ZlinkSubmitException submitExceptionFromSendResult(int rc) {
        return switch (rc) {
            case 1 -> new ZlinkSubmitException(SubmitResult.BACKPRESSURED, 0);
            case 2 -> new ZlinkSubmitException(SubmitResult.NOT_CONNECTED, 0);
            case 0 -> throw new IllegalArgumentException("send result indicates success");
            default -> throw new IllegalArgumentException(
                "invalid send result value: " + rc);
        };
    }

    static ZlinkSubmitException submitExceptionFromSendResult(SendResult result) {
        return switch (result) {
            case BACKPRESSURED -> new ZlinkSubmitException(SubmitResult.BACKPRESSURED, 0);
            case NOT_READY -> new ZlinkSubmitException(SubmitResult.NOT_CONNECTED, 0);
            case SENT -> throw new IllegalArgumentException("send result indicates success");
        };
    }

    private static boolean trySendResult(SendResult result) {
        return switch (result) {
            case SENT -> true;
            case BACKPRESSURED -> false;
            case NOT_READY -> throw submitExceptionFromSendResult(result);
        };
    }

    private void throwPartSubmitFailure(String apiName) {
        int errno = Native.errno();
        ZlinkSubmitException submit = NativeSubmitErrors.submitExceptionOrNull(errno);
        if (submit != null)
            throw submit;
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
        if (InternalAccess.inCallback()
            && (flags.getValue() & SendFlag.DONTWAIT.getValue()) == 0) {
            throw new IllegalStateException(
                "blocking send is not supported from callback context; use SendFlag.DONTWAIT");
        }
    }

    static RoutingId toRoutingId(byte[] value) {
        if (value == null || value.length == 0)
            return null;
        return InternalAccess.routingIdFromTrusted(value);
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

    public void prepareRecvLikeOperation() {
        multipartReceiveState.get().closeRemaining();
        Received active = activeLazyReceive.get();
        if (active != null) {
            InternalAccess.receivedForceMaterialize(active);
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

    public Received registerLazyReceive(Received received, boolean hasMore) {
        if (hasMore) {
            activeLazyReceive.set(received);
        }
        return received;
    }

    private static MemorySegment nativeRoutingId(Arena arena, RoutingId routingId) {
        byte[] value = InternalAccess.routingIdTrustedBytes(routingId);
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
        byte[] value = InternalAccess.routingIdTrustedBytes(routingId);
        if (scratch.cachedRoutingIdBytes == value) {
            return scratch.cachedRoutingIdSegment;
        }
        MemorySegment cachedRid = cachedNativeRoutingId(scratch, value);
        if (cachedRid != null) {
            scratch.cachedRoutingIdBytes = value;
            scratch.cachedRoutingIdSegment = cachedRid;
            return cachedRid;
        }
        MemorySegment nativeRid = scratch.nativeRoutingId;
        writeNativeRoutingId(nativeRid, value);
        scratch.cachedRoutingIdBytes = value;
        scratch.cachedRoutingIdSegment = nativeRid;
        return nativeRid;
    }

    private static MemorySegment nativeRoutingId(SendScratch scratch,
                                                 byte[] value) {
        MemorySegment nativeRid = scratch.nativeRoutingId;
        writeNativeRoutingId(nativeRid, value);
        return nativeRid;
    }

    private static MemorySegment cachedNativeRoutingId(SendScratch scratch,
                                                       byte[] value) {
        byte[][] keys = scratch.cachedRoutingIdKeys;
        MemorySegment[] segments = scratch.cachedRoutingIdSegments;
        if (keys == null || segments == null) {
            keys = new byte[SendScratch.ROUTING_ID_CACHE_CAPACITY][];
            segments = new MemorySegment[SendScratch.ROUTING_ID_CACHE_CAPACITY];
            scratch.cachedRoutingIdKeys = keys;
            scratch.cachedRoutingIdSegments = segments;
        }
        int slot = System.identityHashCode(value)
            & (SendScratch.ROUTING_ID_CACHE_CAPACITY - 1);
        MemorySegment nativeRid = segments[slot];
        if (keys[slot] == value && nativeRid != null) {
            return nativeRid;
        }
        if (nativeRid == null) {
            nativeRid = scratch.arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
            segments[slot] = nativeRid;
        }
        keys[slot] = value;
        writeNativeRoutingId(nativeRid, value);
        return nativeRid;
    }

    private static void writeNativeRoutingId(MemorySegment nativeRid,
                                             byte[] value) {
        nativeRid.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
            (byte) value.length);
        if (value.length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), 0, nativeRid,
                NativeLayouts.ROUTING_ID_DATA_OFFSET, value.length);
        }
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
            var route = optionRoute(optionId);
            int rc = switch (route.family()) {
                case ROUTER -> Native.getRouterOption(handle, route.optionId(), buf, len);
                case PUB -> Native.getPubOption(handle, route.optionId(), buf, len);
                case SUB -> Native.getSubOption(handle, route.optionId(), buf, len);
                case STREAM -> Native.getStreamOption(handle, route.optionId(), buf, len);
                case COMMON -> Native.getSockOpt(handle,
                    route.nativeCommonOptionId(), buf, len);
            };
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_getsockopt");
            return buf.get(ValueLayout.JAVA_LONG, 0);
        }
    }

    private SocketOptionRouter.Route optionRoute(int optionId) {
        return SocketOptionRouter.route(optionId, resolveSocketType());
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

    static int toIntLength(long length) {
        if (length > Integer.MAX_VALUE) {
            throw new IllegalArgumentException("length too large: " + length);
        }
        return (int) length;
    }

    public void ensureOpen() {
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
            byte[] tmp = nettySendArray(length);
            buf.getBytes(readerIndex, tmp, 0, length);
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
            byte[] tmp = nettySendArray(length);
            buf.getBytes(readerIndex, tmp, 0, length);
            sent = sendNoWaitResult(tmp, 0, length, sendFlags);
        }
        if (sent)
            buf.readerIndex(readerIndex + length);
        return sent;
    }

    private byte[] nettySendArray(int length) {
        if (length <= DEFAULT_IO_BUFFER_SIZE)
            return nettySendScratch.get();
        return new byte[length];
    }

    private int recvNettyFallback(ByteBuf buf,
                                  int writerIndex,
                                  int writable,
                                  ReceiveFlag flags) {
        try (Message frame = nextRecvFrame(flags, false)) {
            int rc = Math.min(writable, frame.size());
            if (rc > 0) {
                if (buf.hasArray()) {
                    MemorySegment.copy(InternalAccess.messageDataSegment(frame, rc), 0,
                      MemorySegment.ofArray(buf.array()),
                      (long) buf.arrayOffset() + writerIndex, rc);
                } else {
                    ByteBuffer src = InternalAccess.messageDataSegment(frame).asSlice(0, rc)
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
                    MemorySegment.copy(InternalAccess.messageDataSegment(frame, rc), 0,
                      MemorySegment.ofArray(buf.array()),
                      (long) buf.arrayOffset() + writerIndex, rc);
                } else {
                    ByteBuffer src = InternalAccess.messageDataSegment(frame).asSlice(0, rc)
                      .asByteBuffer();
                    buf.setBytes(writerIndex, src);
                }
                buf.writerIndex(writerIndex + rc);
            }
            return rc;
        }
    }


}
