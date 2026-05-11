/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import systems.zlink.internal.InternalAccess;
import systems.zlink.internal.Native;
import systems.zlink.internal.NativeLayouts;
import systems.zlink.internal.NativeMsg;
import systems.zlink.internal.ReceivedPartCursor;
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
    private final Runnable lazyCompletionRunnable = () -> {
        Received active = activeLazyReceive.get();
        if (active != null) {
            activeLazyReceive.remove();
        }
    };
    private static final ThreadLocal<RecvOutScratch> RECV_OUT_SCRATCH =
        ThreadLocal.withInitial(RecvOutScratch::new);

    private static final class RecvOutScratch {
        final MemorySegment sourceNodeRidOut;
        final MemorySegment sourceSpotRidOut;
        final MemorySegment requestSeqOut;
        final MemorySegment partsOut;
        final MemorySegment partCountOut;
        final MemorySegment hasMoreOut;
        long lastNodeRidPtr;
        byte[] lastNodeRidBytes;
        long lastSpotRidPtr;
        byte[] lastSpotRidBytes;

        RecvOutScratch() {
            Arena auto = Arena.ofAuto();
            sourceNodeRidOut = auto.allocate(ValueLayout.ADDRESS);
            sourceSpotRidOut = auto.allocate(ValueLayout.ADDRESS);
            requestSeqOut = auto.allocate(ValueLayout.JAVA_LONG);
            partsOut = auto.allocate(ValueLayout.ADDRESS);
            partCountOut = auto.allocate(ValueLayout.JAVA_LONG);
            hasMoreOut = auto.allocate(ValueLayout.JAVA_INT);
        }
    }

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
        return recvDirectOnceOrNull(RecvFlags.DONT_WAIT);
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
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        long requestId = RoutedRequestSupport.nextRequestId();
        CompletableFuture<Received> future =
            RoutedRequestSupport.registerPending(requestId, timeoutMs);
        try {
            submitRequest(routingId, parts, timeoutMs, flags,
                RoutedRequestSupport.replyCallback(),
                RoutedRequestSupport.userData(requestId));
            RequestReplySupport.startSocketRequestProgress(future,
                socket.handle(), "zlink-router-request-progress");
        } catch (RuntimeException ex) {
            RoutedRequestSupport.removePending(requestId);
            future.cancel(false);
            throw ex;
        }
        return future;
    }

    private void submitRequest(RoutingId routingId,
                               List<Message> payload,
                               long timeoutMs,
                               SendFlags flags,
                               MemorySegment handler,
                               MemorySegment userData) {
        int nativeFlags = flags == null ? 0 : flags.value();
        int timeout = toTimeoutInt(timeoutMs);
        for (int i = 0; i < payload.size(); i++) {
            int partFlag = i + 1 < payload.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment nativeRid = nativeRoutingId(arena, routingId);
                MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
                payload.get(i).copyTo(nativeMsg);
                int rc = Native.routerRequestPart(socket.handle(), nativeRid,
                    nativeMsg, nativeFlags, partFlag,
                    i + 1 < payload.size() ? 0 : timeout,
                    i + 1 < payload.size() ? MemorySegment.NULL : handler,
                    i + 1 < payload.size() ? MemorySegment.NULL : userData);
                if (rc != SubmitResult.OK.value()) {
                    throw new SubmitException(SubmitResult.fromValue(rc));
                }
            }
        }
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

    private Received recvDirect(RecvFlags flags) {
        Received active = activeLazyReceive.get();
        if (active != null) {
            active.forceMaterialize();
            activeLazyReceive.remove();
        }
        return recvDirectOnce(flags);
    }

    Received recvDirectOnceOrNull(RecvFlags flags) {
        Received active = activeLazyReceive.get();
        if (active != null) {
            active.forceMaterialize();
            activeLazyReceive.remove();
        }
        return recvDirectOnceImpl(flags, true);
    }

    private Received recvDirectOnce(RecvFlags flags) {
        return recvDirectOnceImpl(flags, false);
    }

    private Received recvDirectOnceImpl(RecvFlags flags, boolean nullOnNoData) {
        RecvOutScratch scratch = RECV_OUT_SCRATCH.get();
        MemorySegment sourceNodeRidOut = scratch.sourceNodeRidOut;
        MemorySegment sourceSpotRidOut = scratch.sourceSpotRidOut;
        MemorySegment requestSeqOut = scratch.requestSeqOut;
        MemorySegment hasMoreOut = scratch.hasMoreOut;
        Message firstPart = new Message();
        boolean firstPartConsumed = false;
        try {
            int rc;
            while (true) {
                rc = Native.routerRecvPart(socket.handle(), sourceNodeRidOut,
                    sourceSpotRidOut, requestSeqOut,
                    InternalAccess.messageNativeHandle(firstPart), hasMoreOut,
                    flags.value());
                if (rc == 0) break;
                int errno = Native.errno();
                if (errno == 4) continue;
                RecvResult result = RecvResult.fromValue(rc);
                if (nullOnNoData && (result == RecvResult.NO_DATA
                    || result == RecvResult.BUSY)) {
                    return null;
                }
                throw new RecvException(result, errno);
            }
            boolean hasMore = hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
            InternalAccess.messageFinishReceive(firstPart, hasMore);
            long requestSequence = requestSeqOut.get(ValueLayout.JAVA_LONG, 0);

            if (!hasMore) {
                firstPartConsumed = true;
                if (requestSequence == 0L) {
                    byte[] nodeRidBytes = readRoutingIdBytesOut(sourceNodeRidOut);
                    byte[] spotRidBytes = readRoutingIdBytesOut(sourceSpotRidOut);
                    return new Received(nodeRidBytes, spotRidBytes, firstPart,
                        0L, false, null, lazyCompletionRunnable);
                }
                RoutingId nodeRid = readRoutingIdOut(sourceNodeRidOut);
                RoutingId spotRid = readRoutingIdOut(sourceSpotRidOut);
                return new Received(nodeRid, spotRid, firstPart,
                    requestSequence, true,
                    (replyParts, sendFlags) -> {
                        if (spotRid != null) {
                            socket.replyToSpot(nodeRid, spotRid, requestSequence,
                                replyParts, sendFlags);
                        } else {
                            reply(nodeRid, requestSequence, replyParts, sendFlags);
                        }
                    }, lazyCompletionRunnable);
            }

            java.util.ArrayList<Message> parts = new java.util.ArrayList<>();
            parts.add(firstPart);
            while (hasMore) {
                Message next = new Message();
                boolean nextOk = false;
                try {
                    int rc2 = Native.routerRecvPart(socket.handle(),
                        sourceNodeRidOut, sourceSpotRidOut, requestSeqOut,
                        InternalAccess.messageNativeHandle(next), hasMoreOut,
                        flags.value());
                    if (rc2 != 0) {
                        if (Native.errno() == 4) continue;
                        throw new RecvException(RecvResult.fromValue(rc2),
                            Native.errno());
                    }
                    hasMore = hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                    InternalAccess.messageFinishReceive(next, hasMore);
                    parts.add(next);
                    nextOk = true;
                } finally {
                    if (!nextOk) {
                        try { next.close(); } catch (RuntimeException ignored) {}
                    }
                }
            }
            firstPartConsumed = true;
            Message[] partsArray = parts.toArray(new Message[0]);
            if (requestSequence == 0L) {
                byte[] nodeRidBytes = readRoutingIdBytesOut(sourceNodeRidOut);
                byte[] spotRidBytes = readRoutingIdBytesOut(sourceSpotRidOut);
                return new Received(nodeRidBytes, spotRidBytes, partsArray,
                    true, 0L, false, null, lazyCompletionRunnable);
            }
            RoutingId nodeRid = readRoutingIdOut(sourceNodeRidOut);
            RoutingId spotRid = readRoutingIdOut(sourceSpotRidOut);
            final long capturedSeq = requestSequence;
            return new Received(nodeRid, spotRid, partsArray, true,
                capturedSeq, true,
                (replyParts, sendFlags) -> {
                    if (spotRid != null) {
                        socket.replyToSpot(nodeRid, spotRid, capturedSeq,
                            replyParts, sendFlags);
                    } else {
                        reply(nodeRid, capturedSeq, replyParts, sendFlags);
                    }
                }, lazyCompletionRunnable);
        } finally {
            if (!firstPartConsumed) {
                try {
                    firstPart.close();
                } catch (RuntimeException ignored) {
                }
            }
        }
    }

    private Runnable lazyCompletion() {
        return lazyCompletionRunnable;
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

    private final class RouterReceiveCursor implements ReceivedPartCursor {
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
      implements ReceivedPartCursor {
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

    // Internal optimization for the existing recv() path: probes the
    // RoutingId thread cache off inline (lo, hi, size) words before
    // allocating a byte[]. On cache hit, the byte[] allocation in
    // readRoutingId(...) is avoided. No public API change.
    private static RoutingId readRoutingIdOut(MemorySegment nativeRidOut) {
        MemorySegment nativeRid = nativeRidOut.get(ValueLayout.ADDRESS, 0);
        if (nativeRid.address() == 0) {
            return null;
        }
        nativeRid = nativeRid.reinterpret(
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize());
        int size = nativeRid.get(ValueLayout.JAVA_BYTE,
            NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        if (size == 0) {
            return null;
        }
        if (size <= 16) {
            long lo = nativeRid.get(ValueLayout.JAVA_LONG_UNALIGNED,
                NativeLayouts.ROUTING_ID_DATA_OFFSET);
            long hi = size > 8
                ? nativeRid.get(ValueLayout.JAVA_LONG_UNALIGNED,
                    NativeLayouts.ROUTING_ID_DATA_OFFSET + 8)
                : 0L;
            int loBits = (size >= 8 ? 8 : size) * 8;
            long loMask = loBits == 64 ? -1L : ((1L << loBits) - 1L);
            lo &= loMask;
            int hiBytes = size > 8 ? size - 8 : 0;
            int hiBits = hiBytes * 8;
            long hiMask = hiBits == 64 ? -1L
                : (hiBits == 0 ? 0L : ((1L << hiBits) - 1L));
            hi &= hiMask;
            RoutingId cached = RoutingId.tryFromInlineCached(size, lo, hi);
            if (cached != null) {
                return cached;
            }
        }
        return readRoutingId(nativeRid);
    }


    private static byte[] readRoutingIdBytesOut(MemorySegment nativeRidOut) {
        MemorySegment nativeRid = nativeRidOut.get(ValueLayout.ADDRESS, 0);
        if (nativeRid.address() == 0) {
            return null;
        }
        nativeRid = nativeRid.reinterpret(
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize());
        int size = nativeRid.get(ValueLayout.JAVA_BYTE,
            NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        if (size == 0) {
            return null;
        }
        byte[] value = new byte[size];
        MemorySegment.copy(nativeRid, NativeLayouts.ROUTING_ID_DATA_OFFSET,
            MemorySegment.ofArray(value), 0, size);
        return value;
    }

    private static byte[] readRoutingIdBytesCached(RecvOutScratch scratch,
                                                   MemorySegment nativeRidOut,
                                                   boolean spot) {
        long ptr = nativeRidOut.get(ValueLayout.ADDRESS, 0).address();
        if (ptr == 0L) {
            return null;
        }
        long lastPtr = spot ? scratch.lastSpotRidPtr : scratch.lastNodeRidPtr;
        byte[] lastBytes = spot ? scratch.lastSpotRidBytes
            : scratch.lastNodeRidBytes;
        if (ptr == lastPtr && lastBytes != null) {
            return lastBytes;
        }
        MemorySegment nativeRid = MemorySegment.ofAddress(ptr).reinterpret(
            NativeLayouts.ROUTING_ID_LAYOUT.byteSize());
        int size = nativeRid.get(ValueLayout.JAVA_BYTE,
            NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        if (size == 0) {
            if (spot) {
                scratch.lastSpotRidPtr = ptr;
                scratch.lastSpotRidBytes = null;
            } else {
                scratch.lastNodeRidPtr = ptr;
                scratch.lastNodeRidBytes = null;
            }
            return null;
        }
        byte[] value = new byte[size];
        MemorySegment.copy(nativeRid, NativeLayouts.ROUTING_ID_DATA_OFFSET,
            MemorySegment.ofArray(value), 0, size);
        if (spot) {
            scratch.lastSpotRidPtr = ptr;
            scratch.lastSpotRidBytes = value;
        } else {
            scratch.lastNodeRidPtr = ptr;
            scratch.lastNodeRidBytes = value;
        }
        return value;
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
