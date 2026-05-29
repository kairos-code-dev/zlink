/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.eventing.MonitorEventType;
import systems.zlink.contracts.eventing.SocketMonitor;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.errors.ZlinkConfigException;
import systems.zlink.contracts.errors.ConfigResult;
import systems.zlink.contracts.errors.ZlinkRecvException;
import systems.zlink.runtime.nativeapi.RecvScratch;
import systems.zlink.contracts.core.RoutingId;
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
import systems.zlink.runtime.nativeapi.RequestProgressPump;
import io.netty.buffer.ByteBuf;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
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
    private static final int ERRNO_ENOENT = 2;
    private static final int ERRNO_EINVAL = 22;
    static final int TOPIC_CAPACITY = 256;
    private static final int OPT_RCVMORE = 13;

    private final SocketCore socketCore;
    private final MessagePlane messagePlane;
    private final TopicPlane topicPlane;
    private final ReceivePlane receivePlane;
    private final NettySocketPlane nettyPlane;
    private final SocketSendPlane sendPlane;
    private MemorySegment handle;
    private final boolean own;
    private final SocketType socketTypeHint;
    private final ThreadLocal<RecvScratch> recvScratch =
      ThreadLocal.withInitial(RecvScratch::new);

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
        this.receivePlane = new ReceivePlane(this);
        this.nettyPlane = new NettySocketPlane(this);
        this.sendPlane = new SocketSendPlane(this);
    }

    NativeSocketRuntime(MemorySegment handle, boolean own, SocketType socketTypeHint) {
        this.handle = handle;
        this.own = own;
        this.socketTypeHint = socketTypeHint;
        this.socketCore = new SocketCore(this);
        this.messagePlane = new MessagePlane(this);
        this.topicPlane = new TopicPlane(this);
        this.receivePlane = new ReceivePlane(this);
        this.nettyPlane = new NettySocketPlane(this);
        this.sendPlane = new SocketSendPlane(this);
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
        sendPlane.sendMessageFrame(routingId, message, flag);
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
        return sendPlane.sendMessageFrameNoWaitResult(routingId, message);
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
        return sendPlane.send(routingIdBytes, part, flags);
    }

    void send(int rid, Message part, SendFlag flags) {
        sendPlane.send(rid, part, flags);
    }

    int send(int rid, MemorySegment payload, int length, int sendFlags) {
        return sendPlane.send(rid, payload, length, sendFlags);
    }

    int sendCopied(int rid, MemorySegment payload, int length, int sendFlags) {
        return sendPlane.sendCopied(rid, payload, length, sendFlags);
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
        sendPlane.publishMessageFrame(topicId, message, flags);
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
        return sendPlane.publishMessageFrameNoWaitResult(topicId, message);
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
        return receivePlane.recvInto(result, flags);
    }

    public Optional<Received> recvNoWait() {
        return Optional.ofNullable(recvNoWaitOrNull());
    }

    public Received recvLazy(ReceiveFlag flags) {
        return receivePlane.recvLazy(flags);
    }

    public Received recvLazyNoWaitOrNull() {
        return receivePlane.recvLazyNoWaitOrNull();
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
        return topicPlane.subscribe(result, flags);
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
        return nettyPlane.send(buf, sendFlags);
    }

    boolean sendNoWaitResult(ByteBuf buf, int sendFlags) {
        return nettyPlane.sendNoWaitResult(buf, sendFlags);
    }

    int recv(MemorySegment segment, long offset, long length,
             ReceiveFlag flags) {
        return receivePlane.recv(segment, offset, length, flags);
    }

    int recvNoWait(MemorySegment segment, long offset, long length,
                ReceiveFlag flags) {
        return receivePlane.recvNoWait(segment, offset, length, flags);
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
        return nettyPlane.recvByteBufDirect(buf, flags);
    }

    public int recvByteBufDirectNoWait(ByteBuf buf, ReceiveFlag flags) {
        return nettyPlane.recvByteBufDirectNoWait(buf, flags);
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
            return receivePlane.pendingFrameCount() > 0 ? 1 : 0;
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
        sendPlane.sendMessageFrame(message, flag);
    }

    public boolean sendMessageFrameNoWaitResult(Message message, SendFlag flag) {
        return sendPlane.sendMessageFrameNoWaitResult(message, flag);
    }

    public SendResult sendMessageFrameNoWaitResult(Message message) {
        return sendPlane.sendMessageFrameNoWaitResult(message);
    }

    public void recvMessageFrame(Message message, ReceiveFlag flag) {
        receivePlane.recvMessageFrame(message, flag);
    }

    public int recvMessageFrameNoWait(Message message, ReceiveFlag flag) {
        return receivePlane.recvMessageFrameNoWait(message, flag);
    }

    Message nextRecvFrame(ReceiveFlag flags, boolean nonBlocking) {
        return receivePlane.nextRecvFrame(flags, nonBlocking);
    }

    public void sendParts(RoutingId routingId, List<Message> parts,
                   SendFlag flags, boolean nonBlocking) {
        sendPlane.sendParts(routingId, parts, flags, nonBlocking);
    }

    public SendResult sendNoWaitPartsResult(RoutingId routingId, List<Message> parts) {
        return sendPlane.sendNoWaitPartsResult(routingId, parts);
    }

    public void publishParts(String topicId, List<Message> parts,
                      SendFlag flags, boolean nonBlocking) {
        sendPlane.publishParts(topicId, parts, flags, nonBlocking);
    }

    public SendResult publishNoWaitPartsResult(String topicId, List<Message> parts) {
        return sendPlane.publishNoWaitPartsResult(topicId, parts);
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
        receivePlane.prepareRecvLikeOperation();
    }

    Runnable lazyReceiveCompletion(Received received) {
        return receivePlane.lazyReceiveCompletion(received);
    }

    public Received registerLazyReceive(Received received, boolean hasMore) {
        return receivePlane.registerLazyReceive(received, hasMore);
    }

    RecvScratch recvScratch() {
        return recvScratch.get();
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
                if (errno == ERRNO_ENOENT)
                    return null;
                if (errno == ERRNO_EINVAL) {
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

    static int toIntLength(long length) {
        if (length > Integer.MAX_VALUE) {
            throw new IllegalArgumentException("length too large: " + length);
        }
        return (int) length;
    }

    public void ensureOpen() {
        socketCore.ensureOpen();
    }

}
