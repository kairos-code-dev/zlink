/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.InternalAccess;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import dev.kairoscode.zlink.SocketOptionKey;
import dev.kairoscode.zlink.SocketOptions;
import dev.kairoscode.zlink.SocketOptionValueType;
import dev.kairoscode.zlink.service.discovery.Discovery;
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
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;

final class SocketCore {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_RECV_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_SUBSCRIBE_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
        ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_SEND_READY_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_STREAM_RAW_CALLBACK =
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_STREAM_PACKET_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS);

    /**
     * Tracks whether the current thread is executing inside a native callback
     * (recv handler, subscribe handler, or send-ready handler).
     *
     * <p>Blocking sends from a callback can deadlock the socket I/O thread.
     * The public blocking send APIs therefore reject callback usage explicitly
     * instead of silently downgrading to non-blocking semantics.
     */
    private static final ThreadLocal<Integer> CALLBACK_DEPTH =
        ThreadLocal.withInitial(() -> 0);

    static boolean inCallback() {
        return CALLBACK_DEPTH.get() > 0;
    }

    static void enterCallback() {
        CALLBACK_DEPTH.set(CALLBACK_DEPTH.get() + 1);
    }

    static void leaveCallback() {
        CALLBACK_DEPTH.set(CALLBACK_DEPTH.get() - 1);
    }

    private final Socket socket;
    private Arena sendScratchArena = Arena.ofShared();
    private MemorySegment sendScratch = MemorySegment.NULL;
    private int sendScratchCapacity = Socket.DEFAULT_IO_BUFFER_SIZE;
    private SocketMessageHandler receiveHandler;
    private SubscribeHandler subscribeHandler;
    private SendReadyHandler sendReadyHandler;
    private StreamRawPacketHandler streamPacketHandler;
    private StreamUInt32RawNativeHandler streamUInt32RawNativeHandler;
    private StreamFramedPacketHandler streamFramedPacketHandler;
    private StreamUInt32FramedPacketHandler streamUInt32FramedPacketHandler;
    private StreamUInt32FramedNativeHandler streamUInt32FramedNativeHandler;
    private volatile ExecutorService callbackExecutor;
    private Arena receiveCallbackArena;
    private Arena subscribeCallbackArena;
    private Arena sendReadyCallbackArena;
    private Arena streamRawCallbackArena;
    private Arena streamPacketCallbackArena;
    private final ConcurrentHashMap<Integer, RoutingId> routingIdCache =
      new ConcurrentHashMap<>();
    private volatile RuntimeException callbackFailure;
    private volatile boolean discoveryAttached;

    SocketCore(Socket socket) {
        this.socket = socket;
    }

    void bind(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment addr = arena.allocateFrom(endpoint, StandardCharsets.UTF_8);
            int rc = Native.bind(socket.handle(), addr);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_bind");
        }
    }

    void connect(String endpoint) {
        failIfDiscoveryAttached("zlink_connect");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment addr = arena.allocateFrom(endpoint, StandardCharsets.UTF_8);
            int rc = Native.connect(socket.handle(), addr);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_connect");
        }
    }

    void unbind(String endpoint) {
        failIfDiscoveryAttached("zlink_unbind");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment addr = arena.allocateFrom(endpoint, StandardCharsets.UTF_8);
            int rc = Native.unbind(socket.handle(), addr);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_unbind");
        }
    }

    void disconnect(String endpoint) {
        failIfDiscoveryAttached("zlink_disconnect");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment addr = arena.allocateFrom(endpoint, StandardCharsets.UTF_8);
            int rc = Native.disconnect(socket.handle(), addr);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_disconnect");
        }
    }

    void disconnectRid(RoutingId peerRid) {
        Objects.requireNonNull(peerRid, "peerRid");
        failIfDiscoveryAttached("zlink_disconnect_rid");
        try (Arena arena = Arena.ofConfined()) {
            byte[] value = peerRid.trustedBytes();
            MemorySegment nativeRid = arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
            nativeRid.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
                (byte) value.length);
            if (value.length > 0) {
                MemorySegment.copy(MemorySegment.ofArray(value), 0, nativeRid,
                    NativeLayouts.ROUTING_ID_DATA_OFFSET, value.length);
            }
            int rc = Native.disconnectRid(socket.handle(), nativeRid);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_disconnect_rid");
        }
    }

    void attachDiscovery(Discovery discovery) {
        Objects.requireNonNull(discovery, "discovery");
        int rc = Native.socketAttachDiscovery(socket.handle(),
            InternalAccess.discoveryHandle(discovery));
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_socket_attach_discovery");
        discoveryAttached = true;
    }

    void setTlsServer(String certPem, String keyPem, boolean requireClientCert) {
        socket.setOption(SocketOptions.TLS_CERT, Objects.requireNonNull(certPem, "certPem"));
        socket.setOption(SocketOptions.TLS_KEY, Objects.requireNonNull(keyPem, "keyPem"));
        socket.setOption(SocketOptions.TLS_REQUIRE_CLIENT_CERT,
            requireClientCert ? 1 : 0);
    }

    void setTlsClient(String caCertPem, String hostname, boolean trustSystem) {
        socket.setOption(SocketOptions.TLS_CA, Objects.requireNonNull(caCertPem, "caCertPem"));
        socket.setOption(SocketOptions.TLS_HOSTNAME, Objects.requireNonNull(hostname, "hostname"));
        socket.setOption(SocketOptions.TLS_TRUST_SYSTEM, trustSystem ? 1 : 0);
    }

    void setSockOpt(SocketOption option, byte[] value) {
        Objects.requireNonNull(option, "option");
        socket.validateOptionAccess(option.getValue(), option.name());
        setSockOpt(option, value, 0, value.length);
    }

    void setSockOpt(SocketOption option, byte[] value, int offset, int length) {
        Objects.requireNonNull(option, "option");
        socket.validateOptionAccess(option.getValue(), option.name());
        Objects.requireNonNull(value, "value");
        Socket.validateRange(value.length, offset, length, "value");
        socket.setSockOptBytes(option.getValue(), value, offset, length);
    }

    void setSockOpt(SocketOption option, ByteBuffer value) {
        Objects.requireNonNull(option, "option");
        socket.validateOptionAccess(option.getValue(), option.name());
        Objects.requireNonNull(value, "value");
        int length = value.remaining();
        if (length == 0) {
            socket.setSockOptRaw(option.getValue(), MemorySegment.NULL, 0);
            return;
        }
        MemorySegment srcSeg = MemorySegment.ofBuffer(value);
        MemorySegment seg;
        if (value.isDirect()) {
            seg = srcSeg;
        } else {
            seg = socket.ensureSendScratch(length);
            MemorySegment.copy(srcSeg, 0, seg, 0, length);
        }
        socket.setSockOptRaw(option.getValue(), seg, length);
        value.position(value.position() + length);
    }

    void setSockOpt(SocketOption option, int value) {
        Objects.requireNonNull(option, "option");
        socket.validateOptionAccess(option.getValue(), option.name());
        socket.setSockOptInt(option.getValue(), value);
    }

    byte[] getSockOptBytes(SocketOption option, int maxLen) {
        Objects.requireNonNull(option, "option");
        socket.validateOptionAccess(option.getValue(), option.name());
        return socket.getSockOptBytes(option.getValue(), maxLen);
    }

    int getSockOptInt(SocketOption option) {
        Objects.requireNonNull(option, "option");
        socket.validateOptionAccess(option.getValue(), option.name());
        return socket.getSockOptInt(option.getValue());
    }

    void setOption(SocketOptionKey<Integer> option, int value) {
        Objects.requireNonNull(option, "option");
        socket.validateAmbiguousOption(option);
        socket.validateOptionAccess(option.optionId(), option.name());
        Socket.validateOptionType(option, SocketOptionValueType.INT32);
        option.requireWritable();
        socket.setSockOptInt(option.optionId(), value);
    }

    void setOptionLong(SocketOptionKey<Long> option, long value) {
        Objects.requireNonNull(option, "option");
        socket.validateAmbiguousOption(option);
        socket.validateOptionAccess(option.optionId(), option.name());
        Socket.validateOptionType(option, SocketOptionValueType.INT64);
        option.requireWritable();
        socket.setSockOptLong(option.optionId(), value);
    }

    void setOptionString(SocketOptionKey<String> option, String value) {
        Objects.requireNonNull(option, "option");
        socket.validateAmbiguousOption(option);
        socket.validateOptionAccess(option.optionId(), option.name());
        Socket.validateOptionType(option, SocketOptionValueType.STRING);
        option.requireWritable();
        byte[] utf8 = Objects.requireNonNull(value, "value").getBytes(
            StandardCharsets.UTF_8);
        socket.setTypedBytesOption(option.optionId(), utf8, 0, utf8.length);
    }

    void setOptionBytes(SocketOptionKey<byte[]> option, byte[] value) {
        Objects.requireNonNull(option, "option");
        socket.validateAmbiguousOption(option);
        socket.validateOptionAccess(option.optionId(), option.name());
        Socket.validateOptionType(option, SocketOptionValueType.BYTES);
        option.requireWritable();
        Objects.requireNonNull(value, "value");
        socket.setTypedBytesOption(option.optionId(), value, 0, value.length);
    }

    <T> T getOption(SocketOptionKey<T> option) {
        Objects.requireNonNull(option, "option");
        socket.validateAmbiguousOption(option);
        socket.validateOptionAccess(option.optionId(), option.name());
        option.requireReadable();
        return socket.readOption(option);
    }

    MonitorSocket monitorOpen(int events) {
        MemorySegment sock = Native.monitorOpen(socket.handle(), events);
        if (sock == null || sock.address() == 0)
            throw ZlinkException.fromLastError("zlink_socket_monitor_open");
        return new MonitorSocket(sock, true);
    }

    void onReceive(SocketMessageHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        ensureNoCallbackFailure();
        ExecutorService executor = callbackExecutor;
        boolean createdExecutor = false;
        if (executor == null) {
            executor = newCallbackExecutor();
            callbackExecutor = executor;
            createdExecutor = true;
        }
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(callbackHandle("handleReceiveCallback",
            MethodType.methodType(void.class, MemorySegment.class,
                MemorySegment.class, long.class, MemorySegment.class)),
            FD_RECV_CALLBACK, arena);
        boolean success = false;
        try {
            int rc = Native.recvHandler(socket.handle(), stub, MemorySegment.NULL);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_recv_handler");
            success = true;
            closeArena(receiveCallbackArena);
            receiveCallbackArena = arena;
            receiveHandler = handler;
        } finally {
            if (!success) {
                if (createdExecutor) {
                    callbackExecutor = null;
                    shutdownExecutor(executor);
                }
                closeArena(arena);
            }
        }
    }

    void onSubscribe(SubscribeHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        ensureNoCallbackFailure();
        ExecutorService executor = callbackExecutor;
        boolean createdExecutor = false;
        if (executor == null) {
            executor = newCallbackExecutor();
            callbackExecutor = executor;
            createdExecutor = true;
        }
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(callbackHandle(
            "handleSubscribeCallback", MethodType.methodType(void.class,
                MemorySegment.class, MemorySegment.class, long.class,
                MemorySegment.class, long.class, MemorySegment.class)),
            FD_SUBSCRIBE_CALLBACK, arena);
        boolean success = false;
        try {
            int rc = Native.subscribeHandler(socket.handle(), stub, MemorySegment.NULL);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_subscribe_handler");
            success = true;
            closeArena(subscribeCallbackArena);
            subscribeCallbackArena = arena;
            subscribeHandler = handler;
        } finally {
            if (!success) {
                if (createdExecutor) {
                    callbackExecutor = null;
                    shutdownExecutor(executor);
                }
                closeArena(arena);
            }
        }
    }

    void onSendReady(SendReadyHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        ensureNoCallbackFailure();
        ExecutorService executor = callbackExecutor;
        boolean createdExecutor = false;
        if (executor == null) {
            executor = newCallbackExecutor();
            callbackExecutor = executor;
            createdExecutor = true;
        }
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(callbackHandle(
            "handleSendReadyCallback", MethodType.methodType(void.class,
                MemorySegment.class, MemorySegment.class)),
            FD_SEND_READY_CALLBACK, arena);
        boolean success = false;
        try {
            int rc = Native.sendReadyHandler(socket.handle(), stub, MemorySegment.NULL);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_send_ready_handler");
            success = true;
            closeArena(sendReadyCallbackArena);
            sendReadyCallbackArena = arena;
            sendReadyHandler = handler;
        } finally {
            if (!success) {
                if (createdExecutor) {
                    callbackExecutor = null;
                    shutdownExecutor(executor);
                }
                closeArena(arena);
            }
        }
    }

    void attachStreamRaw(StreamRawPacketHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        ensureNoCallbackFailure();
        ExecutorService executor = callbackExecutor;
        boolean createdExecutor = false;
        if (executor == null) {
            executor = newCallbackExecutor();
            callbackExecutor = executor;
            createdExecutor = true;
        }
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(callbackHandle(
            "handleStreamRawCallback",
            MethodType.methodType(int.class, MemorySegment.class,
                MemorySegment.class, MemorySegment.class)),
            FD_STREAM_RAW_CALLBACK, arena);
        boolean success = false;
        try {
            int rc = Native.streamAttachRaw(socket.handle(), stub);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_stream_attach_raw");
            success = true;
            closeArena(streamRawCallbackArena);
            streamRawCallbackArena = arena;
            streamPacketHandler = handler;
        } finally {
            if (!success) {
                if (createdExecutor) {
                    callbackExecutor = null;
                    shutdownExecutor(executor);
                }
                closeArena(arena);
            }
        }
    }

    void attachStreamRaw(StreamUInt32RawNativeHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        ensureNoCallbackFailure();
        ExecutorService executor = callbackExecutor;
        boolean createdExecutor = false;
        if (executor == null) {
            executor = newCallbackExecutor();
            callbackExecutor = executor;
            createdExecutor = true;
        }
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(callbackHandle(
            "handleStreamRawUInt32NativeCallback",
            MethodType.methodType(int.class, MemorySegment.class,
                MemorySegment.class, MemorySegment.class)),
            FD_STREAM_RAW_CALLBACK, arena);
        boolean success = false;
        try {
            int rc = Native.streamAttachRaw(socket.handle(), stub);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_stream_attach_raw");
            success = true;
            closeArena(streamRawCallbackArena);
            streamRawCallbackArena = arena;
            streamUInt32RawNativeHandler = handler;
        } finally {
            if (!success) {
                if (createdExecutor) {
                    callbackExecutor = null;
                    shutdownExecutor(executor);
                }
                closeArena(arena);
            }
        }
    }

    void attachStreamPacket(StreamFramedPacketHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        ensureNoCallbackFailure();
        ExecutorService executor = callbackExecutor;
        boolean createdExecutor = false;
        if (executor == null) {
            executor = newCallbackExecutor();
            callbackExecutor = executor;
            createdExecutor = true;
        }
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(callbackHandle(
            "handleStreamPacketCallback",
            MethodType.methodType(void.class, MemorySegment.class,
                MemorySegment.class, MemorySegment.class,
                MemorySegment.class, MemorySegment.class)),
            FD_STREAM_PACKET_CALLBACK, arena);
        boolean success = false;
        try {
            int rc = Native.streamPacketHandler(socket.handle(), stub);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_stream_packet_handler");
            success = true;
            closeArena(streamPacketCallbackArena);
            streamPacketCallbackArena = arena;
            streamFramedPacketHandler = handler;
        } finally {
            if (!success) {
                if (createdExecutor) {
                    callbackExecutor = null;
                    shutdownExecutor(executor);
                }
                closeArena(arena);
            }
        }
    }

    void attachStreamPacket(StreamUInt32FramedPacketHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        ensureNoCallbackFailure();
        ExecutorService executor = callbackExecutor;
        boolean createdExecutor = false;
        if (executor == null) {
            executor = newCallbackExecutor();
            callbackExecutor = executor;
            createdExecutor = true;
        }
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(callbackHandle(
            "handleStreamPacketUInt32Callback",
            MethodType.methodType(void.class, MemorySegment.class,
                MemorySegment.class, MemorySegment.class,
                MemorySegment.class, MemorySegment.class)),
            FD_STREAM_PACKET_CALLBACK, arena);
        boolean success = false;
        try {
            int rc = Native.streamPacketHandler(socket.handle(), stub);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_stream_packet_handler");
            success = true;
            closeArena(streamPacketCallbackArena);
            streamPacketCallbackArena = arena;
            streamUInt32FramedPacketHandler = handler;
        } finally {
            if (!success) {
                if (createdExecutor) {
                    callbackExecutor = null;
                    shutdownExecutor(executor);
                }
                closeArena(arena);
            }
        }
    }

    void attachStreamPacket(StreamUInt32FramedNativeHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        ensureNoCallbackFailure();
        ExecutorService executor = callbackExecutor;
        boolean createdExecutor = false;
        if (executor == null) {
            executor = newCallbackExecutor();
            callbackExecutor = executor;
            createdExecutor = true;
        }
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(callbackHandle(
            "handleStreamPacketUInt32NativeCallback",
            MethodType.methodType(void.class, MemorySegment.class,
                MemorySegment.class, MemorySegment.class,
                MemorySegment.class, MemorySegment.class)),
            FD_STREAM_PACKET_CALLBACK, arena);
        boolean success = false;
        try {
            int rc = Native.streamPacketHandler(socket.handle(), stub);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_stream_packet_handler");
            success = true;
            closeArena(streamPacketCallbackArena);
            streamPacketCallbackArena = arena;
            streamUInt32FramedNativeHandler = handler;
        } finally {
            if (!success) {
                if (createdExecutor) {
                    callbackExecutor = null;
                    shutdownExecutor(executor);
                }
                closeArena(arena);
            }
        }
    }

    void detachStream() {
        ensureOpen();
        int rc = Native.streamDetach(socket.handle());
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_stream_detach");
        streamPacketHandler = null;
        streamUInt32RawNativeHandler = null;
        streamFramedPacketHandler = null;
        streamUInt32FramedPacketHandler = null;
        streamUInt32FramedNativeHandler = null;
        closeArena(streamRawCallbackArena);
        closeArena(streamPacketCallbackArena);
        streamRawCallbackArena = null;
        streamPacketCallbackArena = null;
    }

    void close() {
        socket.closeInternal();
    }

    boolean discoveryAttached() {
        return discoveryAttached;
    }

    MemorySegment ensureSendScratch(int length) {
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

    void ensureOpen() {
        if (socket.handle() == null || socket.handle().address() == 0)
            throw new IllegalStateException("socket is closed");
        ensureNoCallbackFailure();
    }

    void ensureNoCallbackFailure() {
        RuntimeException failure = callbackFailure;
        if (failure != null)
            throw failure;
    }

    void closeCommonState() {
        receiveHandler = null;
        subscribeHandler = null;
        sendReadyHandler = null;
        streamPacketHandler = null;
        streamUInt32RawNativeHandler = null;
        streamFramedPacketHandler = null;
        streamUInt32FramedPacketHandler = null;
        streamUInt32FramedNativeHandler = null;
        callbackFailure = null;
        discoveryAttached = false;
        shutdownExecutor(callbackExecutor);
        callbackExecutor = null;
        closeArena(receiveCallbackArena);
        closeArena(subscribeCallbackArena);
        closeArena(sendReadyCallbackArena);
        closeArena(streamRawCallbackArena);
        closeArena(streamPacketCallbackArena);
        receiveCallbackArena = null;
        subscribeCallbackArena = null;
        sendReadyCallbackArena = null;
        streamRawCallbackArena = null;
        streamPacketCallbackArena = null;
        closeArena(sendScratchArena);
        sendScratchArena = null;
        sendScratch = MemorySegment.NULL;
    }

    private void failIfDiscoveryAttached(String operation) {
        if (discoveryAttached) {
            throw ZlinkException.fromErrno(operation, ErrorCode.EFSM.getValue());
        }
    }

    private MethodHandle callbackHandle(String name, MethodType type) {
        try {
            return MethodHandles.lookup().findVirtual(SocketCore.class, name, type)
              .bindTo(this);
        } catch (ReflectiveOperationException ex) {
            throw new IllegalStateException("failed to bind callback " + name, ex);
        }
    }

    private void handleReceiveCallback(MemorySegment sourceRid,
                                       MemorySegment parts,
                                       long partCount,
                                       MemorySegment userdata) {
        SocketMessageHandler handler = receiveHandler;
        ExecutorService executor = callbackExecutor;
        if (handler == null || executor == null)
            return;
        CallbackReceivedData snapshot = null;
        try {
            snapshot = snapshotReceive(sourceRid, parts, partCount);
            CallbackReceivedData callbackSnapshot = snapshot;
            executor.execute(() -> dispatchReceive(handler, callbackSnapshot));
            snapshot = null;
        } catch (RejectedExecutionException ex) {
            recordCallbackFailure(ex);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        } finally {
            closeSnapshot(snapshot);
        }
    }

    private void dispatchReceive(SocketMessageHandler handler,
                                 CallbackReceivedData snapshot) {
        try {
            Received received = materializeReceived(snapshot);
            enterCallback();
            try (received) {
                handler.onMessage(received);
            } finally {
                leaveCallback();
            }
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
        ExecutorService executor = callbackExecutor;
        if (handler == null || executor == null)
            return;
        CallbackSubscribeData snapshot = null;
        try {
            snapshot = snapshotSubscribe(sourceRid, topic, topicLen, parts,
                partCount);
            CallbackSubscribeData callbackSnapshot = snapshot;
            executor.execute(() -> dispatchSubscribe(handler, callbackSnapshot));
            snapshot = null;
        } catch (RejectedExecutionException ex) {
            recordCallbackFailure(ex);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        } finally {
            closeSnapshot(snapshot);
        }
    }

    private void dispatchSubscribe(SubscribeHandler handler,
                                   CallbackSubscribeData snapshot) {
        try {
            Received received = materializeReceived(snapshot);
            enterCallback();
            try (received) {
                handler.onMessage(snapshot.routingId(), snapshot.topicId(),
                    received);
            } finally {
                leaveCallback();
            }
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private void handleSendReadyCallback(MemorySegment subject,
                                         MemorySegment userdata) {
        SendReadyHandler handler = sendReadyHandler;
        ExecutorService executor = callbackExecutor;
        if (handler == null || executor == null)
            return;
        try {
            executor.execute(() -> dispatchSendReady(handler));
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private void dispatchSendReady(SendReadyHandler handler) {
        enterCallback();
        try {
            handler.onReady();
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        } finally {
            leaveCallback();
        }
    }

    private int handleStreamRawCallback(MemorySegment sourceRid,
                                        MemorySegment message,
                                        MemorySegment userdata) {
        StreamRawPacketHandler handler = streamPacketHandler;
        if (handler == null)
            return 0;
        try {
            dispatchStreamRaw(handler, readRoutingId(sourceRid),
                Message.fromOwnedNative(message));
            return 0;
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
            return -1;
        }
    }

    private int handleStreamRawUInt32NativeCallback(MemorySegment sourceRid,
                                                    MemorySegment message,
                                                    MemorySegment userdata) {
        StreamUInt32RawNativeHandler handler = streamUInt32RawNativeHandler;
        if (handler == null)
            return 0;
        try {
            dispatchStreamRawUInt32Native(handler, readRoutingIdUInt32(sourceRid),
                message);
            return 0;
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
            return -1;
        }
    }

    private void handleStreamPacketCallback(MemorySegment stream,
                                            MemorySegment sourceRid,
                                            MemorySegment header,
                                            MemorySegment body,
                                            MemorySegment userdata) {
        StreamFramedPacketHandler handler = streamFramedPacketHandler;
        if (handler == null)
            return;
        try {
            dispatchStreamPacket(handler, readRoutingId(sourceRid),
                Message.fromOwnedNative(header), Message.fromOwnedNative(body));
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private void handleStreamPacketUInt32Callback(MemorySegment stream,
                                                  MemorySegment sourceRid,
                                                  MemorySegment header,
                                                  MemorySegment body,
                                                  MemorySegment userdata) {
        StreamUInt32FramedPacketHandler handler =
            streamUInt32FramedPacketHandler;
        if (handler == null)
            return;
        try {
            dispatchStreamPacketUInt32(handler, readRoutingIdUInt32(sourceRid),
                Message.fromOwnedNative(header), Message.fromOwnedNative(body));
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private void handleStreamPacketUInt32NativeCallback(MemorySegment stream,
                                                        MemorySegment sourceRid,
                                                        MemorySegment header,
                                                        MemorySegment body,
                                                        MemorySegment userdata) {
        StreamUInt32FramedNativeHandler handler =
            streamUInt32FramedNativeHandler;
        if (handler == null)
            return;
        try {
            dispatchStreamPacketUInt32Native(handler,
                readRoutingIdUInt32(sourceRid), header, body);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private void dispatchStreamRaw(StreamRawPacketHandler handler,
                                   RoutingId routingId,
                                   Message payload) {
        enterCallback();
        try (payload) {
            handler.onPacket(routingId, payload);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        } finally {
            leaveCallback();
        }
    }

    private void dispatchStreamRawUInt32Native(
      StreamUInt32RawNativeHandler handler,
      int routingId,
      MemorySegment message) {
        enterCallback();
        try {
            handler.onPacket(routingId, message);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        } finally {
            leaveCallback();
        }
    }

    private void dispatchStreamPacket(StreamFramedPacketHandler handler,
                                      RoutingId routingId,
                                      Message header,
                                      Message body) {
        enterCallback();
        try (header; body) {
            handler.onPacket(routingId, header, body);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        } finally {
            leaveCallback();
        }
    }

    private void dispatchStreamPacketUInt32(StreamUInt32FramedPacketHandler handler,
                                            int routingId,
                                            Message header,
                                            Message body) {
        enterCallback();
        try (header; body) {
            handler.onPacket(routingId, header, body);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        } finally {
            leaveCallback();
        }
    }

    private void dispatchStreamPacketUInt32Native(
      StreamUInt32FramedNativeHandler handler,
      int routingId,
      MemorySegment header,
      MemorySegment body) {
        enterCallback();
        try {
            handler.onPacket(routingId, header, body);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        } finally {
            leaveCallback();
        }
    }

    private CallbackReceivedData snapshotReceive(MemorySegment sourceRid,
                                                 MemorySegment parts,
                                                 long partCount) {
        Message[] snapshotParts = Message.fromOwnedMsgVectorShared(parts,
            partCount);
        NativeMsg.multipartClose(parts, partCount);
        return new CallbackReceivedData(readRoutingId(sourceRid), snapshotParts);
    }

    private CallbackSubscribeData snapshotSubscribe(MemorySegment sourceRid,
                                                    MemorySegment topic,
                                                    long topicLen,
                                                    MemorySegment parts,
                                                    long partCount) {
        Message[] snapshotParts = Message.fromOwnedMsgVectorShared(parts,
            partCount);
        NativeMsg.multipartClose(parts, partCount);
        return new CallbackSubscribeData(readRoutingId(sourceRid),
            decodeTopic(topic, topicLen), snapshotParts);
    }

    private static Received materializeReceived(CallbackReceivedData snapshot) {
        return new Received(snapshot.routingId(), null, snapshot.parts(), true,
          0L, false,
          null);
    }

    private static Received materializeReceived(CallbackSubscribeData snapshot) {
        return new Received(snapshot.routingId(), null, snapshot.parts(), true,
          0L, false,
          null);
    }

    private RoutingId readRoutingId(MemorySegment sourceRid) {
        if (sourceRid == null || sourceRid.address() == 0)
            return null;
        MemorySegment routingId = sourceRid.reinterpret(
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize());
        int size = routingId.get(ValueLayout.JAVA_BYTE,
            NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        if (size == 0)
            return null;
        if (size == Integer.BYTES) {
            int key = 0;
            for (int i = 0; i < Integer.BYTES; i++) {
                key = (key << 8) | (routingId.get(ValueLayout.JAVA_BYTE,
                    NativeLayouts.ROUTING_ID_DATA_OFFSET + i) & 0xFF);
            }
            final int routingKey = key;
            return routingIdCache.computeIfAbsent(routingKey, unused -> {
                byte[] cached = new byte[Integer.BYTES];
                for (int i = 0; i < Integer.BYTES; i++) {
                    cached[i] = routingId.get(ValueLayout.JAVA_BYTE,
                        NativeLayouts.ROUTING_ID_DATA_OFFSET + i);
                }
                return RoutingId.fromTrusted(cached);
            });
        }
        byte[] value = new byte[size];
        MemorySegment.copy(routingId, NativeLayouts.ROUTING_ID_DATA_OFFSET,
            MemorySegment.ofArray(value), 0, size);
        return RoutingId.fromTrusted(value);
    }

    private int readRoutingIdUInt32(MemorySegment sourceRid) {
        if (sourceRid == null || sourceRid.address() == 0)
            throw new IllegalStateException("missing routing id");
        MemorySegment routingId = sourceRid.reinterpret(
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize());
        int size = routingId.get(ValueLayout.JAVA_BYTE,
            NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        if (size != Integer.BYTES) {
            throw new IllegalStateException(
                "STREAM uint32 callback requires a 4-byte routing id");
        }
        return ((routingId.get(ValueLayout.JAVA_BYTE,
            NativeLayouts.ROUTING_ID_DATA_OFFSET) & 0xFF) << 24)
            | ((routingId.get(ValueLayout.JAVA_BYTE,
                NativeLayouts.ROUTING_ID_DATA_OFFSET + 1) & 0xFF) << 16)
            | ((routingId.get(ValueLayout.JAVA_BYTE,
                NativeLayouts.ROUTING_ID_DATA_OFFSET + 2) & 0xFF) << 8)
            | (routingId.get(ValueLayout.JAVA_BYTE,
                NativeLayouts.ROUTING_ID_DATA_OFFSET + 3) & 0xFF);
    }

    private static String decodeTopic(MemorySegment topic, long topicLen) {
        int length = Socket.toIntLength(topicLen);
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

    private static void closeArena(Arena arena) {
        if (arena != null && arena.scope().isAlive())
            arena.close();
    }

    private static ExecutorService newCallbackExecutor() {
        return Executors.newSingleThreadExecutor(runnable -> {
            Thread thread = new Thread(runnable, "zlink-socket-callback");
            thread.setDaemon(true);
            return thread;
        });
    }

    private static void closeReceived(Received received) {
        if (received != null) {
            try {
                received.close();
            } catch (RuntimeException ignored) {
            }
        }
    }

    private static void closeSnapshot(CallbackReceivedData snapshot) {
        if (snapshot != null)
            closeMessages(snapshot.parts());
    }

    private static void closeSnapshot(CallbackSubscribeData snapshot) {
        if (snapshot != null)
            closeMessages(snapshot.parts());
    }

    private static void closeMessages(Message[] parts) {
        if (parts == null)
            return;
        for (Message part : parts) {
            if (part == null)
                continue;
            try {
                part.close();
            } catch (RuntimeException ignored) {
            }
        }
    }

    private static void shutdownExecutor(ExecutorService executor) {
        if (executor != null)
            executor.shutdown();
    }

    private record CallbackReceivedData(RoutingId routingId,
                                        Message[] parts) {}

    private record CallbackSubscribeData(RoutingId routingId, String topicId,
                                         Message[] parts) {}
}
