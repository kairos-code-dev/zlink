/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import dev.kairoscode.zlink.internal.NativeRequestReplyBridge;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.LockSupport;
import java.util.function.BiConsumer;

final class RouterRequestSupport implements AutoCloseable {
    private static final long BLOCKING_RECV_POLL_NANOS = 100_000L;
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_ROUTER_HANDLER =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
        ValueLayout.ADDRESS);

    private final RouterSocket socket;
    private final boolean closeSocketOnClose;
    private volatile SocketMessageHandler dataHandler;
    private volatile ExecutorService callbackExecutor;
    private volatile RuntimeException callbackFailure;
    private Arena receiveCallbackArena;
    private boolean handlerRegistered;
    private volatile boolean closed;
    private final ThreadLocal<Received> activeLazyReceive = new ThreadLocal<>();

    RouterRequestSupport(RouterSocket socket) {
        this(socket, true);
    }

    RouterRequestSupport(RouterSocket socket, boolean closeSocketOnClose) {
        this.socket = Objects.requireNonNull(socket, "socket");
        this.closeSocketOnClose = closeSocketOnClose;
    }

    public RouterSocket socket() {
        return socket;
    }

    public CompletableFuture<Received> request(RoutingId routingId, Message part) {
        return request(routingId, List.of(part));
    }

    public CompletableFuture<Received> request(RoutingId routingId,
                                               Message part,
                                               SendFlags flags) {
        return request(routingId, List.of(part), flags);
    }

    public CompletableFuture<Received> request(RoutingId routingId, List<Message> parts) {
        return request(routingId, parts, SendFlags.NONE);
    }

    public CompletableFuture<Received> request(RoutingId routingId,
                                               List<Message> parts,
                                               SendFlags flags) {
        return requestInternal(routingId, parts,
            Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS),
            flags);
    }

    public CompletableFuture<Received> request(RoutingId routingId,
                                               Message part,
                                               Duration timeout) {
        return request(routingId, List.of(part), timeout);
    }

    public CompletableFuture<Received> request(RoutingId routingId,
                                               Message part,
                                               SendFlags flags,
                                               Duration timeout) {
        return request(routingId, List.of(part), flags, timeout);
    }

    public CompletableFuture<Received> request(RoutingId routingId,
                                               List<Message> parts,
                                               Duration timeout) {
        return request(routingId, parts, SendFlags.NONE, timeout);
    }

    public CompletableFuture<Received> request(RoutingId routingId,
                                               List<Message> parts,
                                               SendFlags flags,
                                               Duration timeout) {
        return requestInternal(routingId, parts, timeout, flags);
    }

    public void request(RoutingId routingId,
                        Message part,
                        BiConsumer<RequestResult, Received> callback) {
        request(routingId, List.of(part), callback);
    }

    public void request(RoutingId routingId,
                        Message part,
                        BiConsumer<RequestResult, Received> callback,
                        SendFlags flags) {
        request(routingId, List.of(part), callback, flags);
    }

    public void request(RoutingId routingId,
                        Message part,
                        BiConsumer<RequestResult, Received> callback,
                        SendFlags flags, Duration timeout) {
        request(routingId, List.of(part), callback, flags, timeout);
    }

    public void request(RoutingId routingId,
                        List<Message> parts,
                        BiConsumer<RequestResult, Received> callback) {
        request(routingId, parts, callback, SendFlags.NONE,
            Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }

    public void request(RoutingId routingId,
                        List<Message> parts,
                        BiConsumer<RequestResult, Received> callback,
                        SendFlags flags) {
        request(routingId, parts, callback, flags,
            Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }

    public void request(RoutingId routingId,
                        List<Message> parts,
                        BiConsumer<RequestResult, Received> callback,
                        SendFlags flags, Duration timeout) {
        Objects.requireNonNull(callback, "callback");
        requestInternal(routingId, parts, timeout, flags).whenComplete(
            (reply, error) -> callback.accept(
                error == null ? RequestResult.OK
                    : RequestReplySupport.requestResult(error),
                reply));
    }

    public void reply(RoutingId routingId, long requestSequence, Message part) {
        reply(routingId, requestSequence, List.of(part));
    }

    public void reply(RoutingId routingId, long requestSequence, Message part,
                      SendFlags flags) {
        reply(routingId, requestSequence, List.of(part), flags);
    }

    public void reply(RoutingId routingId, long requestSequence,
                      List<Message> parts) {
        reply(routingId, requestSequence, parts, SendFlags.NONE);
    }

    public void reply(RoutingId routingId, long requestSequence,
                      List<Message> parts, SendFlags flags) {
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(parts, "parts");
        RequestReplySupport.requireReplyFlagsSupported(flags);
        for (int i = 0; i < parts.size(); i++) {
            int partFlag = i + 1 < parts.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = routerReplyPartOnce(routingId, requestSequence,
                    parts.get(i), partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == Socket.ERRNO_EINTR)
                    continue;
                throw submitFailure("zlink_router_reply_part");
            }
        }
    }

    public Received recv() {
        return recv(RecvFlags.NONE);
    }

    public Received recv(RecvFlags flags) {
        Objects.requireNonNull(flags, "flags");
        if (dataHandler != null) {
            throw new IllegalStateException(
                "socket is in callback mode; direct recv is not allowed");
        }
        if (flags == RecvFlags.DONT_WAIT) {
            Received received = recvNoWaitOrNull();
            if (received == null) {
                throw new RecvException(RecvResult.NO_DATA,
                    Socket.ERRNO_EAGAIN);
            }
            return received;
        }
        return recvDirect(flags);
    }

    public void onReceive(SocketMessageHandler handler) {
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

        Arena arena = null;
        boolean createdHandler = false;
        if (!handlerRegistered) {
            arena = Arena.ofShared();
            MemorySegment stub = LINKER.upcallStub(callbackHandle(
                "handleReceiveCallback",
                MethodType.methodType(void.class, MemorySegment.class,
                    MemorySegment.class, long.class, MemorySegment.class,
                    long.class, MemorySegment.class)),
                FD_ROUTER_HANDLER, arena);
            int rc = NativeMsg.routerHandler(socket.handle(), stub,
                MemorySegment.NULL);
            if (rc != 0) {
                if (createdExecutor) {
                    callbackExecutor = null;
                    shutdownExecutor(executor);
                }
                closeArena(arena);
                throw ZlinkException.fromLastError("zlink_router_handler");
            }
            receiveCallbackArena = arena;
            handlerRegistered = true;
            createdHandler = true;
        }

        try {
            dataHandler = handler;
        } catch (RuntimeException ex) {
            if (createdHandler) {
                closeArena(receiveCallbackArena);
                receiveCallbackArena = null;
                handlerRegistered = false;
            }
            if (createdExecutor) {
                callbackExecutor = null;
                shutdownExecutor(executor);
            }
            throw ex;
        }
    }

    Received recvNoWaitOrNull() {
        if (dataHandler != null) {
            throw new IllegalStateException(
                "socket is in callback mode; direct recv is not allowed");
        }
        try {
            return recvDirectOnce(RecvFlags.DONT_WAIT);
        } catch (RecvException ex) {
            if (ex.getResult() == RecvResult.NO_DATA) {
                return null;
            }
            throw ex;
        }
    }

    Optional<Received> recvNoWait() {
        return Optional.ofNullable(recvNoWaitOrNull());
    }

    @Override
    public void close() {
        beginClose();
        try {
            if (closeSocketOnClose) {
                socket.close();
            }
        } finally {
            finishClose();
        }
    }

    void beginClose() {
        if (closed) {
            return;
        }
        closed = true;
        dataHandler = null;
        shutdownExecutor(callbackExecutor);
        callbackExecutor = null;
    }

    void finishClose() {
        closeArena(receiveCallbackArena);
        receiveCallbackArena = null;
    }

    private void handleReceiveCallback(MemorySegment sourceNodeRid,
                                       MemorySegment sourceSpotRid,
                                       long requestSequence,
                                       MemorySegment parts,
                                       long partCount,
                                       MemorySegment userdata) {
        SocketMessageHandler handler = dataHandler;
        ExecutorService executor = callbackExecutor;
        if (handler == null || executor == null) {
            NativeMsg.multipartClose(parts, partCount);
            return;
        }
        CallbackReceivedData snapshot;
        try {
            snapshot = snapshotReceive(sourceNodeRid, sourceSpotRid,
                requestSequence, parts, partCount);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
            return;
        }
        try {
            executor.execute(() -> dispatchReceive(handler, snapshot));
        } catch (RejectedExecutionException ex) {
            recordCallbackFailure(ex);
        }
    }

    private CallbackReceivedData snapshotReceive(MemorySegment sourceNodeRid,
                                                 MemorySegment sourceSpotRid,
                                                 long requestSequence,
                                                 MemorySegment parts,
                                                 long partCount) {
        Message[] snapshotParts;
        try {
            snapshotParts = Message.fromOwnedMsgVectorShared(parts, partCount);
        } finally {
            NativeMsg.multipartClose(parts, partCount);
        }
        return new CallbackReceivedData(readRoutingId(sourceNodeRid),
            readRoutingId(sourceSpotRid), requestSequence, snapshotParts);
    }

    private void dispatchReceive(SocketMessageHandler handler,
                                 CallbackReceivedData snapshot) {
        RoutingId nodeRid = snapshot.nodeRid();
        RoutingId spotRid = snapshot.spotRid();
        long requestSequence = snapshot.requestSequence();
        try (Received received = new Received(nodeRid, spotRid,
            snapshot.parts(), true, requestSequence, requestSequence != 0L,
            requestSequence == 0L ? null : (replyParts, sendFlags) -> {
                if (spotRid != null) {
                    socket.replyToSpot(nodeRid, spotRid, requestSequence,
                        replyParts, sendFlags);
                } else {
                    reply(nodeRid, requestSequence, replyParts, sendFlags);
                }
            })) {
            handler.onMessage(received);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private CompletableFuture<Received> requestInternal(RoutingId routingId,
                                                        List<Message> parts,
                                                        Duration timeout,
                                                        SendFlags flags) {
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(parts, "parts");
        List<Message> payload = RequestReplySupport.clonePayload(parts);
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        return RequestReplySupport.startTimedRequestExecution(
            () -> submitRequest(routingId, payload, timeoutMs, flags),
            timeoutMs);
    }

    private Received submitRequest(RoutingId routingId,
                                   List<Message> payload,
                                   long timeoutMs,
                                   SendFlags flags) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeRid = nativeRoutingId(arena, routingId);
            MemorySegment nativeParts = movePayloadToNative(arena, payload);
            NativeRequestReplyBridge.ReplyResult reply =
                NativeRequestReplyBridge.routerRequestSync(
                    socket.handle(), nativeRid, nativeParts, payload.size(),
                    flags == null ? 0 : flags.value(), toTimeoutInt(timeoutMs));
            if (reply.submitResult() != SubmitResult.OK.value()) {
                RequestReplySupport.closeAll(payload);
                throw new SubmitException(SubmitResult.fromValue(reply.submitResult()));
            }
            if (reply.requestResult() != RequestResult.OK.value()) {
                throw new RequestException(
                    RequestResult.fromValue(reply.requestResult()),
                    reply.requestResult());
            }
            RequestReplySupport.closeAll(payload);
            return new Received((RoutingId) null, (RoutingId) null,
                Message.fromMsgVector(reply.replyParts(), reply.replyPartCount()),
                true, 0L, false, null);
        } catch (Throwable error) {
            RequestReplySupport.closeAll(payload);
            if (error instanceof SubmitException submitException) {
                throw submitException;
            }
            if (error instanceof RequestException requestException) {
                throw requestException;
            }
            throw error instanceof RuntimeException runtimeException
                ? runtimeException
                : new IllegalStateException("request submission failed", error);
        }
    }

    private static MemorySegment movePayloadToNative(Arena arena,
                                                     List<Message> payload) {
        long msgSize = NativeLayouts.MSG_LAYOUT.byteSize();
        MemorySegment nativeParts = arena.allocate(msgSize * payload.size(),
            NativeLayouts.MSG_LAYOUT.byteAlignment());
        int built = 0;
        try {
            for (int i = 0; i < payload.size(); i++) {
                payload.get(i).transferTo(nativeParts.asSlice((long) i * msgSize,
                    msgSize));
                built++;
            }
            return nativeParts;
        } catch (RuntimeException ex) {
            for (int i = built; i < payload.size(); i++) {
                try {
                    payload.get(i).close();
                } catch (RuntimeException ignored) {
                }
            }
            throw ex;
        }
    }

    private static MemorySegment nativeRoutingId(Arena arena, RoutingId routingId) {
        byte[] value = routingId.toBytes();
        MemorySegment nativeRid = arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
        nativeRid.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
            (byte) value.length);
        if (value.length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), 0, nativeRid,
                NativeLayouts.ROUTING_ID_DATA_OFFSET, value.length);
        }
        return nativeRid;
    }

    private Received recvDirect(RecvFlags flags) {
        Received active = activeLazyReceive.get();
        if (active != null) {
            active.forceMaterialize();
            activeLazyReceive.remove();
        }
        return recvDirectOnce(flags);
    }

    private Received recvDirectOnce(RecvFlags flags) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment sourceNodeRidOut = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment sourceSpotRidOut = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment requestSeqOut = arena.allocate(ValueLayout.JAVA_LONG);
            MemorySegment partsOut = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment partCountOut = arena.allocate(ValueLayout.JAVA_LONG);
            int rc = Native.routerRecv(socket.handle(), sourceNodeRidOut,
                sourceSpotRidOut, requestSeqOut, partsOut, partCountOut,
                flags.value());
            if (rc != 0) {
                throw new RecvException(RecvResult.fromValue(rc),
                    Native.errno());
            }

            AggregateRouterReceiveCursor cursor =
                new AggregateRouterReceiveCursor(
                    partsOut.get(ValueLayout.ADDRESS, 0),
                    partCountOut.get(ValueLayout.JAVA_LONG, 0));
            Message firstPart = cursor.nextPartOrNull();
            if (firstPart == null) {
                throw new RecvException(RecvResult.NO_DATA);
            }

            boolean hasMore = firstPart.more();
            RoutingId nodeRid = readRoutingIdOut(sourceNodeRidOut);
            RoutingId spotRid = readRoutingIdOut(sourceSpotRidOut);
            long requestSequence = requestSeqOut.get(ValueLayout.JAVA_LONG, 0);
            byte[] nodeRidBytes = nodeRid == null ? null : nodeRid.toBytes();
            byte[] spotRidBytes = spotRid == null ? null : spotRid.toBytes();
            Received.PartCursor remainingCursor = hasMore ? cursor : null;
            return registerLazyReceive(new Received(nodeRidBytes, spotRidBytes,
                firstPart, remainingCursor, requestSequence,
                requestSequence != 0L, requestSequence == 0L ? null
                : (replyParts, sendFlags) -> {
                    if (spotRid != null) {
                        socket.replyToSpot(nodeRid, spotRid, requestSequence,
                            replyParts, sendFlags);
                    } else {
                        reply(nodeRid, requestSequence, replyParts, sendFlags);
                    }
                }, lazyCompletion()), hasMore);
        }
    }

    private Runnable lazyCompletion() {
        return () -> {
            Received active = activeLazyReceive.get();
            if (active != null) {
                activeLazyReceive.remove();
            }
        };
    }

    private Received registerLazyReceive(Received received, boolean hasMore) {
        if (hasMore) {
            activeLazyReceive.set(received);
        }
        return received;
    }

    private int routerReplyPartOnce(RoutingId routingId, long requestSequence,
                                    Message part, int partFlag) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeRid = nativeRoutingId(arena, routingId);
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            Object anchor = part.transferTo(nativeMsg);
            try {
                int rc = Native.routerReplyPart(socket.handle(), nativeRid,
                    requestSequence, nativeMsg, partFlag);
                if (rc != 0) {
                    part.restoreFromNative(nativeMsg, false, anchor);
                }
                return rc;
            } catch (RuntimeException ex) {
                part.restoreFromNative(nativeMsg, false, anchor);
                throw ex;
            }
        }
    }

    private SubmitException submitFailure(String apiName) {
        int errno = Native.errno();
        if (errno == Socket.ERRNO_EAGAIN
            || errno == Socket.ERRNO_EWOULDBLOCK_WIN) {
            return new SubmitException(SubmitResult.BACKPRESSURED, errno);
        }
        if (errno == Socket.ERRNO_ENOTCONN
            || errno == Socket.ERRNO_ENOTCONN_WIN
            || errno == Socket.ERRNO_EHOSTUNREACH
            || errno == Socket.ERRNO_EHOSTUNREACH_WIN) {
            return new SubmitException(SubmitResult.NOT_CONNECTED, errno);
        }
        throw ZlinkException.fromLastError(apiName);
    }

    private final class RouterReceiveCursor implements Received.PartCursor {
        private final Arena arena = Arena.ofConfined();
        private final MemorySegment sourceNodeRidOut = arena.allocate(
            ValueLayout.ADDRESS);
        private final MemorySegment sourceSpotRidOut = arena.allocate(
            ValueLayout.ADDRESS);
        private final MemorySegment requestSeqOut = arena.allocate(
            ValueLayout.JAVA_LONG);
        private final MemorySegment hasMoreOut = arena.allocate(
            ValueLayout.JAVA_INT);
        private boolean hasMore = true;
        private boolean closed;

        @Override
        public Message nextPartOrNull() {
            if (closed || !hasMore)
                return null;
            while (true) {
                Message next = new Message();
                boolean success = false;
                try {
                    int rc = Native.routerRecvPart(socket.handle(),
                        sourceNodeRidOut, sourceSpotRidOut, requestSeqOut,
                        next.nativeHandle(), hasMoreOut,
                        RecvFlags.DONT_WAIT.value());
                    if (rc == 0) {
                        success = true;
                        hasMore = hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                        next.finishReceive(hasMore);
                        if (!hasMore) {
                            closeArena();
                        }
                        return next;
                    }
                    if (rc == RecvResult.NO_DATA.value()) {
                        LockSupport.parkNanos(BLOCKING_RECV_POLL_NANOS);
                        continue;
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
                if (errno == Socket.ERRNO_EINTR)
                    continue;
                closeArena();
                throw ZlinkException.fromLastError("zlink_router_recv_part");
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

    private final class AggregateRouterReceiveCursor
      implements Received.PartCursor {
        private final MemorySegment partsAddr;
        private final long partCount;
        private final MemorySegment parts;
        private long nextIndex;
        private boolean closed;
        private boolean vectorClosed;

        private AggregateRouterReceiveCursor(MemorySegment partsAddr,
                                             long partCount) {
            this.partsAddr = partsAddr;
            this.partCount = Math.max(0L, partCount);
            long msgSize = NativeLayouts.MSG_LAYOUT.byteSize();
            this.parts = this.partCount == 0 || partsAddr == null
                || partsAddr.address() == 0
                ? MemorySegment.NULL
                : MemorySegment.ofAddress(partsAddr.address()).reinterpret(
                    msgSize * this.partCount);
        }

        @Override
        public Message nextPartOrNull() {
            if (closed || nextIndex >= partCount)
                return null;
            long msgSize = NativeLayouts.MSG_LAYOUT.byteSize();
            Message next = new Message();
            boolean success = false;
            try {
                MemorySegment src = parts.asSlice(nextIndex * msgSize, msgSize);
                int rc = NativeMsg.msgMove(next.nativeHandle(), src);
                if (rc != 0) {
                    throw ZlinkException.fromLastError("zlink_msg_move");
                }
                nextIndex++;
                next.finishReceive(nextIndex < partCount);
                success = true;
                if (nextIndex >= partCount) {
                    closeVector();
                }
                return next;
            } finally {
                if (!success) {
                    try {
                        next.close();
                    } catch (RuntimeException ignored) {
                    }
                }
            }
        }

        @Override
        public void close() {
            if (closed)
                return;
            closed = true;
            closeVector();
        }

        private void closeVector() {
            if (!vectorClosed && partsAddr != null && partsAddr.address() != 0) {
                vectorClosed = true;
                // Native.routerRecv() exposes a thread-local multipart view.
                // Close the moved-from parts, but do not free the backing array.
                NativeMsg.multipartClose(partsAddr, partCount);
            }
        }
    }

    private MethodHandle callbackHandle(String name, MethodType type) {
        try {
            return MethodHandles.lookup().findVirtual(
                RouterRequestSupport.class, name, type).bindTo(this);
        } catch (ReflectiveOperationException ex) {
            throw new IllegalStateException("failed to bind callback " + name, ex);
        }
    }

    private void ensureOpen() {
        if (closed || socket.handle() == null || socket.handle().address() == 0) {
            throw new IllegalStateException("socket is closed");
        }
    }

    private void ensureNoCallbackFailure() {
        RuntimeException failure = callbackFailure;
        if (failure != null) {
            throw failure;
        }
    }

    private void recordCallbackFailure(RuntimeException failure) {
        callbackFailure = failure;
        Thread current = Thread.currentThread();
        Thread.UncaughtExceptionHandler uncaught =
            current.getUncaughtExceptionHandler();
        if (uncaught != null) {
            uncaught.uncaughtException(current, failure);
        }
    }

    private static ExecutorService newCallbackExecutor() {
        return Executors.newSingleThreadExecutor(runnable -> {
            Thread thread = new Thread(runnable, "zlink-router-callback");
            thread.setDaemon(true);
            return thread;
        });
    }

    private static void shutdownExecutor(ExecutorService executor) {
        if (executor != null) {
            executor.shutdown();
            try {
                executor.awaitTermination(1, TimeUnit.SECONDS);
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
            }
        }
    }

    private static void closeArena(Arena arena) {
        if (arena != null && arena.scope().isAlive()) {
            arena.close();
        }
    }

    private static int toTimeoutInt(long timeoutMs) {
        if (timeoutMs <= 1L) {
            return 1;
        }
        return timeoutMs >= Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) timeoutMs;
    }

    private static RoutingId readRoutingIdOut(MemorySegment nativeRidOut) {
        MemorySegment nativeRid = nativeRidOut.get(ValueLayout.ADDRESS, 0);
        if (nativeRid.address() != 0) {
            nativeRid = nativeRid.reinterpret(
                NativeLayouts.ROUTING_ID_LAYOUT.byteSize());
        }
        return readRoutingId(nativeRid);
    }

    private static RoutingId readRoutingId(MemorySegment nativeRid) {
        if (nativeRid == null || nativeRid.address() == 0) {
            return null;
        }
        if (nativeRid.byteSize() < NativeLayouts.ROUTING_ID_LAYOUT.byteSize()) {
            nativeRid = nativeRid.reinterpret(
                NativeLayouts.ROUTING_ID_LAYOUT.byteSize());
        }
        int size = nativeRid.get(ValueLayout.JAVA_BYTE,
            NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        if (size == 0) {
            return null;
        }
        byte[] value = new byte[size];
        MemorySegment.copy(nativeRid, NativeLayouts.ROUTING_ID_DATA_OFFSET,
            MemorySegment.ofArray(value), 0, size);
        return RoutingId.fromTrusted(value);
    }

    private record CallbackReceivedData(RoutingId nodeRid, RoutingId spotRid,
                                        long requestSequence, Message[] parts) {}
}
