/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.InternalAccess;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import dev.kairoscode.zlink.options.SocketOptionKey;
import dev.kairoscode.zlink.options.SocketOptions;
import dev.kairoscode.zlink.options.SocketOptionValueType;
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

    /**
     * Tracks whether the current thread is executing inside a native callback
     * (recv handler, subscribe handler, or send-ready handler).
     *
     * <p>Blocking sends from a callback run on the socket I/O thread.  If the
     * send needs to retry (pipe full / EAGAIN), the retry loop blocks on
     * {@code process_commands} which waits for I/O events that can only be
     * delivered by the very same I/O thread, causing a deadlock.
     *
     * <p>All send paths check this flag and force {@code DONTWAIT} when inside
     * a callback so the I/O thread is never blocked in a retry loop.
     */
    private static final ThreadLocal<Integer> CALLBACK_DEPTH =
        ThreadLocal.withInitial(() -> 0);

    static boolean inCallback() {
        return CALLBACK_DEPTH.get() > 0;
    }

    private static void enterCallback() {
        CALLBACK_DEPTH.set(CALLBACK_DEPTH.get() + 1);
    }

    private static void leaveCallback() {
        CALLBACK_DEPTH.set(CALLBACK_DEPTH.get() - 1);
    }

    private final Socket socket;
    private Arena sendScratchArena = Arena.ofShared();
    private MemorySegment sendScratch = MemorySegment.NULL;
    private int sendScratchCapacity = Socket.DEFAULT_IO_BUFFER_SIZE;
    private SocketMessageHandler receiveHandler;
    private SubscribeHandler subscribeHandler;
    private SendReadyHandler sendReadyHandler;
    private Arena receiveCallbackArena;
    private Arena subscribeCallbackArena;
    private Arena sendReadyCallbackArena;
    private volatile RuntimeException callbackFailure;

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
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment addr = arena.allocateFrom(endpoint, StandardCharsets.UTF_8);
            int rc = Native.connect(socket.handle(), addr);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_connect");
        }
    }

    void unbind(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment addr = arena.allocateFrom(endpoint, StandardCharsets.UTF_8);
            int rc = Native.unbind(socket.handle(), addr);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_unbind");
        }
    }

    void disconnect(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment addr = arena.allocateFrom(endpoint, StandardCharsets.UTF_8);
            int rc = Native.disconnect(socket.handle(), addr);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_disconnect");
        }
    }

    void attachDiscovery(Discovery discovery) {
        Objects.requireNonNull(discovery, "discovery");
        int rc = Native.socketAttachDiscovery(socket.handle(),
            InternalAccess.discoveryHandle(discovery));
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_socket_attach_discovery");
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
            if (!success)
                closeArena(arena);
        }
    }

    void onSubscribe(SubscribeHandler handler) {
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
            int rc = Native.subscribeHandler(socket.handle(), stub, MemorySegment.NULL);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_subscribe_handler");
            success = true;
            closeArena(subscribeCallbackArena);
            subscribeCallbackArena = arena;
            subscribeHandler = handler;
        } finally {
            if (!success)
                closeArena(arena);
        }
    }

    void onSendReady(SendReadyHandler handler) {
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
            int rc = Native.sendReadyHandler(socket.handle(), stub, MemorySegment.NULL);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_send_ready_handler");
            success = true;
            closeArena(sendReadyCallbackArena);
            sendReadyCallbackArena = arena;
            sendReadyHandler = handler;
        } finally {
            if (!success)
                closeArena(arena);
        }
    }

    void close() {
        socket.closeInternal();
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
        callbackFailure = null;
        closeArena(receiveCallbackArena);
        closeArena(subscribeCallbackArena);
        closeArena(sendReadyCallbackArena);
        receiveCallbackArena = null;
        subscribeCallbackArena = null;
        sendReadyCallbackArena = null;
        closeArena(sendScratchArena);
        sendScratchArena = null;
        sendScratch = MemorySegment.NULL;
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
        if (handler == null)
            return;
        enterCallback();
        try (Received received = receivedFromCallback(sourceRid, parts, partCount)) {
            handler.onMessage(received);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        } finally {
            leaveCallback();
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
        enterCallback();
        try (Received received = receivedFromCallback(sourceRid, parts, partCount)) {
            handler.onMessage(readRoutingId(sourceRid), decodeTopic(topic, topicLen),
                received);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        } finally {
            leaveCallback();
        }
    }

    private void handleSendReadyCallback(MemorySegment subject,
                                         MemorySegment userdata) {
        SendReadyHandler handler = sendReadyHandler;
        if (handler == null)
            return;
        enterCallback();
        try {
            handler.onReady();
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        } finally {
            leaveCallback();
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
}
