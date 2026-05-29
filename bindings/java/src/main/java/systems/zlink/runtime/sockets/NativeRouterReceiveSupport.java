/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkRecvException;
import systems.zlink.contracts.errors.ZlinkException;
import systems.zlink.internal.ContractAccess;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RecvResult;
import systems.zlink.contracts.sockets.RouterSocket;
import systems.zlink.contracts.sockets.SocketMessageHandler;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeMessage;
import systems.zlink.runtime.nativeapi.RuntimeResources;
import systems.zlink.runtime.messaging.ReceivedPartCursor;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.LockSupport;

final class NativeRouterReceiveSupport implements AutoCloseable {
    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final long BLOCKING_RECV_POLL_NANOS = 100_000L;
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_ROUTER_HANDLER =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
        ValueLayout.ADDRESS);

    private final NativeRouterSocket socket;
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

    NativeRouterReceiveSupport(RouterSocket socket) {
        this(socket, true);
    }

    NativeRouterReceiveSupport(RouterSocket socket, boolean closeSocketOnClose) {
        this.socket = (NativeRouterSocket) Objects.requireNonNull(socket,
            "socket");
        this.closeSocketOnClose = closeSocketOnClose;
    }

    public RouterSocket socket() {
        return socket;
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
                throw new ZlinkRecvException(RecvResult.NO_DATA,
                    ERRNO_EAGAIN);
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
            int rc = NativeMessage.routerHandler(InternalAccess.socketHandle(socket), stub,
                MemorySegment.NULL);
            if (rc != 0) {
                if (createdExecutor) {
                    callbackExecutor = null;
                    RuntimeResources.shutdownExecutor(executor,
                        1, TimeUnit.SECONDS);
                }
                RuntimeResources.closeArena(arena);
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
                RuntimeResources.closeArena(receiveCallbackArena);
                receiveCallbackArena = null;
                handlerRegistered = false;
            }
            if (createdExecutor) {
                callbackExecutor = null;
                RuntimeResources.shutdownExecutor(executor,
                    1, TimeUnit.SECONDS);
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

    /**
     * Canonical caller-provided storage recv. Populates {@code target}
     * directly when the routed recv yields a single-part non-request-seq
     * message (the routed-echo hot path); for multipart or request-seq
     * results falls through to the allocation fallback path so the
     * surface keeps the same observable semantics across recv shapes.
     * Returns {@code true} on data, {@code false} on EAGAIN with
     * {@link RecvFlags#DONT_WAIT}.
     */
    public boolean recvInto(Received target, RecvFlags flags) {
        Objects.requireNonNull(target, "target");
        Objects.requireNonNull(flags, "flags");
        if (dataHandler != null) {
            throw new IllegalStateException(
                "socket is in callback mode; direct recv is not allowed");
        }
        boolean dontWait = (flags == RecvFlags.DONT_WAIT);
        return recvDirectOnceIntoImpl(target, flags, dontWait);
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

    public void beginClose() {
        if (closed) {
            return;
        }
        closed = true;
        dataHandler = null;
        RuntimeResources.shutdownExecutor(callbackExecutor,
            1, TimeUnit.SECONDS);
        callbackExecutor = null;
    }

    public void finishClose() {
        RuntimeResources.closeArena(receiveCallbackArena);
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
            NativeMessage.multipartClose(parts, partCount);
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
            snapshotParts = InternalAccess.messageFromOwnedMessageVectorShared(parts, partCount);
        } finally {
            NativeMessage.multipartClose(parts, partCount);
        }
        return new CallbackReceivedData(readRoutingId(sourceNodeRid),
            readRoutingId(sourceSpotRid), requestSequence, snapshotParts);
    }

    private void dispatchReceive(SocketMessageHandler handler,
                                 CallbackReceivedData snapshot) {
        RoutingId nodeRid = snapshot.nodeRid();
        RoutingId spotRid = snapshot.spotRid();
        long requestSequence = snapshot.requestSequence();
        try (Received received = InternalAccess.received(nodeRid, spotRid, snapshot.parts(), true, requestSequence, requestSequence != 0L, requestSequence == 0L ? null : (replyParts, sendFlags) -> {
                if (spotRid != null) {
                    InternalAccess.routerReplyToSpot(socket, nodeRid, spotRid, requestSequence,
                            replyParts, sendFlags);
                } else {
                    InternalAccess.routerReply(socket, nodeRid,
                        requestSequence, replyParts, sendFlags);
                }
            })) {
            handler.onMessage(received);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    /**
     * Variant of {@link #recvDirectOnceImpl} that, when the result is a
     * single-part non-request-seq routed message (no spot, no request
     * sequence — the routed echo hot path), populates {@code target} in
     * place via {@link Received#populateRoutedSinglePart}, avoiding the
     * fresh {@link Received} allocation used by the fallback path.
     * Other paths fall back to the existing impl + {@link Received#adoptFrom}.
     */
    private boolean recvDirectOnceIntoImpl(Received target, RecvFlags flags,
                                           boolean nullOnNoData) {
        Received active = activeLazyReceive.get();
        if (active != null) {
            InternalAccess.receivedForceMaterialize(active);
            activeLazyReceive.remove();
        }

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
                rc = routerRecvPart(sourceNodeRidOut, sourceSpotRidOut,
                    requestSeqOut,
                    InternalAccess.messageNativeHandle(firstPart), hasMoreOut,
                    flags.value());
                if (rc == 0) break;
                int errno = Native.errno();
                if (errno == 4) continue;
                RecvResult result = RecvResult.fromValue(rc);
                if (nullOnNoData && (result == RecvResult.NO_DATA
                    || result == RecvResult.BUSY)) {
                    return false;
                }
                throw new ZlinkRecvException(result, errno);
            }
            boolean hasMore = hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
            InternalAccess.messageFinishReceive(firstPart, hasMore);
            long requestSequence = requestSeqOut.get(ValueLayout.JAVA_LONG, 0);

            if (!hasMore && requestSequence == 0L) {
                // Routed echo hot path: populate caller storage in place.
                byte[] nodeRidBytes = readRoutingIdBytesOut(sourceNodeRidOut);
                byte[] spotRidBytes = readRoutingIdBytesOut(sourceSpotRidOut);
                firstPartConsumed = true;
                ContractAccess.receivedPopulateRoutedSinglePart(target,
                    nodeRidBytes, spotRidBytes, firstPart, 0L, false, null,
                    lazyCompletionRunnable);
                if (nodeRidBytes != null) {
                    socket.attachSendSender(target);
                }
                return true;
            }

            // Cold path (multipart or request-seq): fall back to the
            // allocate-and-adopt implementation so surface semantics stay
            // identical for non-echo routed recv shapes (request-reply,
            // multipart envelopes).
            firstPartConsumed = continueFallbackAdopt(target, firstPart, hasMore,
                requestSequence, scratch, flags);
            return true;
        } finally {
            if (!firstPartConsumed) {
                try { firstPart.close(); } catch (RuntimeException ignored) {}
            }
        }
    }

    private boolean continueFallbackAdopt(Received target, Message firstPart,
                                          boolean hasMore, long requestSequence,
                                          RecvOutScratch scratch, RecvFlags flags) {
        // Reconstruct a fresh Received via the existing constructors for the
        // multipart / request-seq case, then adoptFrom into the target.
        MemorySegment sourceNodeRidOut = scratch.sourceNodeRidOut;
        MemorySegment sourceSpotRidOut = scratch.sourceSpotRidOut;
        MemorySegment requestSeqOut = scratch.requestSeqOut;
        MemorySegment hasMoreOut = scratch.hasMoreOut;
        Received fresh;
        if (!hasMore) {
            RoutingId nodeRid = readRoutingIdOut(sourceNodeRidOut);
            RoutingId spotRid = readRoutingIdOut(sourceSpotRidOut);
            fresh = InternalAccess.receivedLazy(nodeRid, spotRid, firstPart,
                null,
                requestSequence, true,
                (replyParts, sendFlags) -> {
                    if (spotRid != null) {
                        InternalAccess.routerReplyToSpot(socket, nodeRid, spotRid, requestSequence,
                            replyParts, sendFlags);
                    } else {
                        InternalAccess.routerReply(socket, nodeRid,
                            requestSequence, replyParts, sendFlags);
                    }
                }, lazyCompletionRunnable);
        } else {
            java.util.ArrayList<Message> parts = new java.util.ArrayList<>();
            parts.add(firstPart);
            boolean stillMore = true;
            while (stillMore) {
                Message next = new Message();
                boolean nextOk = false;
                try {
                    int rc = routerRecvPart(sourceNodeRidOut, sourceSpotRidOut,
                        requestSeqOut,
                        InternalAccess.messageNativeHandle(next), hasMoreOut,
                        flags.value());
                    if (rc != 0) {
                        if (Native.errno() == 4) continue;
                        throw new ZlinkRecvException(RecvResult.fromValue(rc),
                            Native.errno());
                    }
                    stillMore = hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                    InternalAccess.messageFinishReceive(next, stillMore);
                    parts.add(next);
                    nextOk = true;
                } finally {
                    if (!nextOk) {
                        try { next.close(); } catch (RuntimeException ignored) {}
                    }
                }
            }
            Message[] partsArray = parts.toArray(new Message[0]);
            if (requestSequence == 0L) {
                byte[] nodeRidBytes = readRoutingIdBytesOut(sourceNodeRidOut);
                byte[] spotRidBytes = readRoutingIdBytesOut(sourceSpotRidOut);
                fresh = InternalAccess.received(nodeRidBytes, spotRidBytes, partsArray, true, 0L, false, null, lazyCompletionRunnable);
            } else {
                RoutingId nodeRid = readRoutingIdOut(sourceNodeRidOut);
                RoutingId spotRid = readRoutingIdOut(sourceSpotRidOut);
                fresh = InternalAccess.received(nodeRid, spotRid, partsArray, true, requestSequence, true, (replyParts, sendFlags) -> {
                        if (spotRid != null) {
                            InternalAccess.routerReplyToSpot(socket, nodeRid, spotRid, requestSequence,
                            replyParts, sendFlags);
                        } else {
                            InternalAccess.routerReply(socket, nodeRid,
                                requestSequence, replyParts, sendFlags);
                        }
                    }, lazyCompletionRunnable);
            }
        }
        target.adoptFrom(fresh);
        return true;
    }

    private Received recvDirect(RecvFlags flags) {
        Received active = activeLazyReceive.get();
        if (active != null) {
            InternalAccess.receivedForceMaterialize(active);
            activeLazyReceive.remove();
        }
        return recvDirectOnce(flags);
    }

    Received recvDirectOnceOrNull(RecvFlags flags) {
        Received active = activeLazyReceive.get();
        if (active != null) {
            InternalAccess.receivedForceMaterialize(active);
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
                rc = routerRecvPart(sourceNodeRidOut, sourceSpotRidOut,
                    requestSeqOut,
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
                throw new ZlinkRecvException(result, errno);
            }
            boolean hasMore = hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
            InternalAccess.messageFinishReceive(firstPart, hasMore);
            long requestSequence = requestSeqOut.get(ValueLayout.JAVA_LONG, 0);

            if (!hasMore) {
                firstPartConsumed = true;
                if (requestSequence == 0L) {
                    byte[] nodeRidBytes = readRoutingIdBytesOut(sourceNodeRidOut);
                    byte[] spotRidBytes = readRoutingIdBytesOut(sourceSpotRidOut);
                    return InternalAccess.receivedLazy(nodeRidBytes, spotRidBytes,
                        firstPart, null,
                        0L, false, null, lazyCompletionRunnable);
                }
                RoutingId nodeRid = readRoutingIdOut(sourceNodeRidOut);
                RoutingId spotRid = readRoutingIdOut(sourceSpotRidOut);
                return InternalAccess.receivedLazy(nodeRid, spotRid, firstPart,
                    null,
                    requestSequence, true,
                    (replyParts, sendFlags) -> {
                        if (spotRid != null) {
                            InternalAccess.routerReplyToSpot(socket, nodeRid, spotRid, requestSequence,
                            replyParts, sendFlags);
                        } else {
                            InternalAccess.routerReply(socket, nodeRid,
                                requestSequence, replyParts, sendFlags);
                        }
                    }, lazyCompletionRunnable);
            }

            java.util.ArrayList<Message> parts = new java.util.ArrayList<>();
            parts.add(firstPart);
            while (hasMore) {
                Message next = new Message();
                boolean nextOk = false;
                try {
                    int rc2 = routerRecvPart(sourceNodeRidOut, sourceSpotRidOut,
                        requestSeqOut,
                        InternalAccess.messageNativeHandle(next), hasMoreOut,
                        flags.value());
                    if (rc2 != 0) {
                        if (Native.errno() == 4) continue;
                        throw new ZlinkRecvException(RecvResult.fromValue(rc2),
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
                return InternalAccess.received(nodeRidBytes, spotRidBytes, partsArray, true, 0L, false, null, lazyCompletionRunnable);
            }
            RoutingId nodeRid = readRoutingIdOut(sourceNodeRidOut);
            RoutingId spotRid = readRoutingIdOut(sourceSpotRidOut);
            final long capturedSeq = requestSequence;
            return InternalAccess.received(nodeRid, spotRid, partsArray, true, capturedSeq, true, (replyParts, sendFlags) -> {
                    if (spotRid != null) {
                        InternalAccess.routerReplyToSpot(socket, nodeRid, spotRid, capturedSeq,
                            replyParts, sendFlags);
                    } else {
                        InternalAccess.routerReply(socket, nodeRid,
                            capturedSeq, replyParts, sendFlags);
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

    private int routerRecvPart(MemorySegment sourceNodeRidOut,
                               MemorySegment sourceSpotRidOut,
                               MemorySegment requestSeqOut,
                               MemorySegment partOut,
                               MemorySegment hasMoreOut,
                               int flags) {
        if ((flags & RecvFlags.DONT_WAIT.value()) != 0) {
            return Native.routerRecvPartNoWaitCritical(InternalAccess.socketHandle(socket),
                sourceNodeRidOut, sourceSpotRidOut, requestSeqOut, partOut,
                hasMoreOut, flags);
        }
        return Native.routerRecvPart(InternalAccess.socketHandle(socket), sourceNodeRidOut,
            sourceSpotRidOut, requestSeqOut, partOut, hasMoreOut, flags);
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
                    int rc = routerRecvPart(sourceNodeRidOut, sourceSpotRidOut,
                        requestSeqOut,
                        InternalAccess.messageNativeHandle(next), hasMoreOut,
                        RecvFlags.DONT_WAIT.value());
                    if (rc == 0) {
                        success = true;
                        hasMore = hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                        InternalAccess.messageFinishReceive(next, hasMore);
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
                if (errno == ERRNO_EINTR)
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
            long messageSize = NativeLayouts.MESSAGE_LAYOUT.byteSize();
            this.parts = this.partCount == 0 || partsAddr == null
                || partsAddr.address() == 0
                ? MemorySegment.NULL
                : MemorySegment.ofAddress(partsAddr.address()).reinterpret(
                    messageSize * this.partCount);
        }

        @Override
        public Message nextPartOrNull() {
            if (closed || nextIndex >= partCount)
                return null;
            long messageSize = NativeLayouts.MESSAGE_LAYOUT.byteSize();
            Message next = new Message();
            boolean success = false;
            try {
                MemorySegment src = parts.asSlice(nextIndex * messageSize, messageSize);
                int rc = NativeMessage.messageMove(InternalAccess.messageNativeHandle(next), src);
                if (rc != 0) {
                    throw ZlinkException.fromLastError("zlink_msg_move");
                }
                nextIndex++;
                InternalAccess.messageFinishReceive(next, nextIndex < partCount);
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
                NativeMessage.multipartClose(partsAddr, partCount);
            }
        }
    }

    private MethodHandle callbackHandle(String name, MethodType type) {
        try {
            return MethodHandles.lookup().findVirtual(
                NativeRouterReceiveSupport.class, name, type).bindTo(this);
        } catch (ReflectiveOperationException ex) {
            throw new IllegalStateException("failed to bind callback " + name, ex);
        }
    }

    private void ensureOpen() {
        if (closed || InternalAccess.socketHandle(socket) == null || InternalAccess.socketHandle(socket).address() == 0) {
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
        return RuntimeResources.daemonSingleThreadExecutor(
            "zlink-router-callback");
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
        return InternalAccess.routingIdFromTrusted(value);
    }

    private record CallbackReceivedData(RoutingId nodeRid, RoutingId spotRid,
                                        long requestSequence, Message[] parts) {}
}
