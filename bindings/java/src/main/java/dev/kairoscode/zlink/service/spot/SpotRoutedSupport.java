/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.HandlerException;
import dev.kairoscode.zlink.HandlerResult;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.Received;
import dev.kairoscode.zlink.RecvException;
import dev.kairoscode.zlink.RecvFlags;
import dev.kairoscode.zlink.RecvResult;
import dev.kairoscode.zlink.RequestException;
import dev.kairoscode.zlink.RequestResult;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.SpotDispatchInfo;
import dev.kairoscode.zlink.SpotDispatchEvent;
import dev.kairoscode.zlink.SpotDispatchEventHandler;
import dev.kairoscode.zlink.SpotDispatchSubjectKind;
import dev.kairoscode.zlink.SpotRoutedHandler;
import dev.kairoscode.zlink.SubmitException;
import dev.kairoscode.zlink.SubmitResult;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.internal.InternalAccess;
import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
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
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.BiConsumer;

final class SpotRoutedSupport implements AutoCloseable {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_REPLY_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_SPOT_HANDLER =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
        ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_DISPATCH_HANDLER =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS);
    private static final Arena CALLBACK_ARENA = Arena.ofShared();
    private static final MemorySegment REPLY_CALLBACK = LINKER.upcallStub(
      callbackHandle("handleReplyCallback", MethodType.methodType(void.class,
        int.class, MemorySegment.class, long.class, MemorySegment.class)),
      FD_REPLY_CALLBACK, CALLBACK_ARENA);
    private static final AtomicLong NEXT_REQUEST_ID = new AtomicLong(1L);
    private static final ConcurrentMap<Long, CompletableFuture<Received>> PENDING =
      new ConcurrentHashMap<>();
    private static final ScheduledExecutorService REQUEST_TIMEOUTS =
      Executors.newSingleThreadScheduledExecutor(new TimeoutThreadFactory());

    private final Spot spot;
    private SpotRoutedHandler routedHandler;
    private SpotDispatchEventHandler dispatchEventHandler;
    private ExecutorService callbackExecutor;
    private Arena routedCallbackArena;
    private Arena dispatchCallbackArena;
    private volatile RuntimeException callbackFailure;
    private final ThreadLocal<Received> activeLazyReceive = new ThreadLocal<>();

    SpotRoutedSupport(Spot spot) {
        this.spot = Objects.requireNonNull(spot, "spot");
    }

    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, List<Message> parts, SendFlags flags) {
        requireReplyFlagsSupported(flags);
        List<Message> payload = clonePayload(parts);
        try {
            submitSpotReplySpot(Objects.requireNonNull(destNodeRid,
                "destNodeRid"), Objects.requireNonNull(destSpotRid,
                "destSpotRid"), requestSeq, payload);
        } finally {
            closeAll(payload);
        }
    }

    void replyToRouter(RoutingId peerRid, long requestSeq, List<Message> parts,
                       SendFlags flags) {
        requireReplyFlagsSupported(flags);
        List<Message> payload = clonePayload(parts);
        try {
            submitSpotReplyRouter(Objects.requireNonNull(peerRid, "peerRid"),
                requestSeq, payload);
        } finally {
            closeAll(payload);
        }
    }

    Received recvRouted(RecvFlags flags) {
        Objects.requireNonNull(flags, "flags");
        ensureOpen();
        Received active = activeLazyReceive.get();
        if (active != null) {
            InternalAccess.receivedForceMaterialize(active);
            activeLazyReceive.remove();
        }
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment sourceRidOut = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment spotRidOut = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment requestSeqOut = arena.allocate(ValueLayout.JAVA_LONG);
            MemorySegment hasMoreOut = arena.allocate(ValueLayout.JAVA_INT);
            Message firstPart = new Message();
            boolean success = false;
            try {
                int rc = Native.spotRecvPart(handle(), sourceRidOut, spotRidOut,
                    requestSeqOut, InternalAccess.messageNativeHandle(firstPart),
                    hasMoreOut, flags.value());
                if (rc != 0) {
                    throw new RecvException(RecvResult.fromValue(rc));
                }
                success = true;
                boolean hasMore = hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                InternalAccess.messageFinishReceive(firstPart, hasMore);
                RoutingId source = readRoutingIdOut(sourceRidOut);
                RoutingId sourceSpot = readRoutingIdOut(spotRidOut);
                long requestSeq = requestSeqOut.get(ValueLayout.JAVA_LONG, 0);
                Received.PartCursor cursor = hasMore
                    ? new SpotReceiveCursor(flags.value()) : null;
                Received[] ref = new Received[1];
                Runnable onTerminal = () -> {
                    Received pending = activeLazyReceive.get();
                    if (pending == ref[0]) {
                        activeLazyReceive.remove();
                    }
                };
                Received received = InternalAccess.receivedLazy(source,
                    sourceSpot, firstPart, cursor, requestSeq,
                    requestSeq != 0L, requestSeq == 0L ? null
                    : (replyParts, sendFlags) -> {
                        if (sourceSpot != null) {
                            replyToSpot(source, sourceSpot, requestSeq,
                                replyParts, sendFlags);
                        } else {
                            replyToRouter(source, requestSeq, replyParts,
                                sendFlags);
                        }
                    }, onTerminal);
                ref[0] = received;
                if (hasMore) {
                    activeLazyReceive.set(received);
                }
                return received;
            } finally {
                if (!success) {
                    try {
                        firstPart.close();
                    } catch (RuntimeException ignored) {
                    }
                }
            }
        }
    }

    void onRoutedReceive(SpotRoutedHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        ensureNoCallbackFailure();
        ExecutorService executor = ensureCallbackExecutor("zlink-spot-routed-callback");
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(callbackHandle(
          "handleRoutedCallback", MethodType.methodType(void.class,
            MemorySegment.class, MemorySegment.class, long.class,
            MemorySegment.class, long.class, MemorySegment.class)),
          FD_SPOT_HANDLER, arena);
        boolean success = false;
        try {
            int rc = Native.spotHandler(handle(), stub, MemorySegment.NULL);
            if (rc != 0) {
                throw new HandlerException(HandlerResult.fromValue(rc));
            }
            success = true;
            closeArena(routedCallbackArena);
            routedCallbackArena = arena;
            routedHandler = handler;
        } finally {
            if (!success) {
                closeArena(arena);
                if (callbackExecutor == executor && routedHandler == null
                    && dispatchEventHandler == null) {
                    shutdownExecutor(executor);
                    callbackExecutor = null;
                }
            }
        }
    }

    void onDispatchEvent(SpotDispatchEventHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        ensureNoCallbackFailure();
        ExecutorService executor = ensureCallbackExecutor("zlink-spot-dispatch-callback");
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(callbackHandle(
          "handleDispatchEventCallback",
          MethodType.methodType(void.class, MemorySegment.class,
            MemorySegment.class, MemorySegment.class), this),
          FD_DISPATCH_HANDLER, arena);
        boolean success = false;
        try {
            int rc = Native.spotDispatchEventHandler(handle(), stub,
              MemorySegment.NULL);
            if (rc != 0) {
                throw new HandlerException(HandlerResult.fromValue(rc));
            }
            success = true;
            closeArena(dispatchCallbackArena);
            dispatchCallbackArena = arena;
            dispatchEventHandler = handler;
        } finally {
            if (!success) {
                closeArena(arena);
                if (callbackExecutor == executor && routedHandler == null
                    && dispatchEventHandler == null) {
                    shutdownExecutor(executor);
                    callbackExecutor = null;
                }
            }
        }
    }

    @Override
    public void close() {
        routedHandler = null;
        dispatchEventHandler = null;
        callbackFailure = null;
        shutdownExecutor(callbackExecutor);
        callbackExecutor = null;
        closeArena(routedCallbackArena);
        closeArena(dispatchCallbackArena);
        routedCallbackArena = null;
        dispatchCallbackArena = null;
    }

    private void handleRoutedCallback(MemorySegment sourceRid,
                                      MemorySegment spotRid,
                                      long requestSeq,
                                      MemorySegment parts,
                                      long partCount,
                                      MemorySegment userdata) {
        SpotRoutedHandler handler = routedHandler;
        ExecutorService executor = callbackExecutor;
        if (handler == null || executor == null) {
            NativeMsg.multipartClose(parts, partCount);
            return;
        }
        RoutedSnapshot snapshot = null;
        try {
            Message[] frames = InternalAccess.messageFromOwnedMsgVectorShared(
              parts, partCount);
            snapshot = new RoutedSnapshot(readRoutingId(sourceRid),
              readRoutingId(spotRid), requestSeq, frames);
            NativeMsg.multipartClose(parts, partCount);
            RoutedSnapshot callbackSnapshot = snapshot;
            executor.execute(() -> dispatchRouted(handler, callbackSnapshot));
        } catch (RejectedExecutionException ex) {
            recordCallbackFailure(ex);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private void handleDispatchEventCallback(MemorySegment spotHandle,
                                             MemorySegment info,
                                             MemorySegment userdata) {
        SpotDispatchEventHandler handler = dispatchEventHandler;
        ExecutorService executor = callbackExecutor;
        if (handler == null || executor == null) {
            return;
        }
        try {
            SpotDispatchInfo dispatchInfo = decodeDispatchInfo(info);
            executor.execute(() -> dispatchEvent(handler, dispatchInfo));
        } catch (RejectedExecutionException ex) {
            recordCallbackFailure(ex);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private void dispatchRouted(SpotRoutedHandler handler,
                                RoutedSnapshot snapshot) {
        try {
            Received received = InternalAccess.received(snapshot.sourceRid(),
              snapshot.spotRid(), snapshot.parts(), snapshot.requestSeq(),
              snapshot.requestSeq() != 0L,
              snapshot.requestSeq() == 0L ? null : (replyParts, sendFlags) -> {
                  if (snapshot.spotRid() != null) {
                      replyToSpot(snapshot.sourceRid(), snapshot.spotRid(),
                        snapshot.requestSeq(), replyParts, sendFlags);
                  } else {
                      replyToRouter(snapshot.sourceRid(), snapshot.requestSeq(),
                        replyParts, sendFlags);
                  }
              });
            InternalAccess.enterCallback();
            try (received) {
                handler.onMessage(snapshot.sourceRid(), snapshot.spotRid(),
                  snapshot.requestSeq(), received);
            } finally {
                InternalAccess.leaveCallback();
            }
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private void dispatchEvent(SpotDispatchEventHandler handler,
                               SpotDispatchInfo info) {
        InternalAccess.enterCallback();
        try {
            handler.onEvent(info);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        } finally {
            InternalAccess.leaveCallback();
        }
    }

    private static SpotDispatchInfo decodeDispatchInfo(MemorySegment info) {
        if (info == null || info.address() == 0) {
            throw new IllegalArgumentException("dispatch info must not be null");
        }
        info = info.reinterpret(NativeLayouts.SPOT_DISPATCH_INFO_LAYOUT.byteSize());
        SpotDispatchEvent event = SpotDispatchEvent.fromValue(info.get(
          ValueLayout.JAVA_INT, NativeLayouts.SPOT_DISPATCH_INFO_EVENT_OFFSET));
        SpotDispatchSubjectKind subjectKind = SpotDispatchSubjectKind.fromValue(
          info.get(ValueLayout.JAVA_INT,
            NativeLayouts.SPOT_DISPATCH_INFO_SUBJECT_KIND_OFFSET));
        MemorySegment subject = info.get(ValueLayout.ADDRESS,
          NativeLayouts.SPOT_DISPATCH_INFO_SUBJECT_OFFSET);
        return new SpotDispatchInfo(event, subjectKind, subject);
    }

    private CompletableFuture<List<Message>> requestViaNative(List<Message> parts,
                                                              Duration timeout,
                                                              NativeRequest request) {
        long timeoutMs = timeoutMillis(timeout);
        long requestId = NEXT_REQUEST_ID.getAndIncrement();
        List<Message> payload = clonePayload(parts);
        CompletableFuture<Received> future = registerPending(requestId, timeoutMs);
        try (Arena arena = Arena.ofConfined()) {
            int rc = request.invoke(arena, payload, requestId, timeoutMs);
            if (rc != 0) {
                closeAll(payload);
                future.cancel(false);
                throw new SubmitException(SubmitResult.fromValue(rc));
            }
        } catch (RuntimeException ex) {
            closeAll(payload);
            PENDING.remove(requestId);
            future.cancel(false);
            throw ex;
        }
        return future.thenApply(InternalAccess::receivedTakeParts);
    }

    private void requestViaNativeCallback(List<Message> parts,
                                          BiConsumer<RequestResult, List<Message>> callback,
                                          Duration timeout,
                                          NativeRequest request) {
        Objects.requireNonNull(callback, "callback");
        requestViaNative(parts, timeout, request)
          .whenComplete((reply, error) -> callback.accept(error == null
            ? RequestResult.OK : requestResult(error),
            reply == null ? List.of() : reply));
    }

    private void sendViaNative(List<Message> parts, NativeSubmit submitter) {
        List<Message> payload = clonePayload(parts);
        try (Arena arena = Arena.ofConfined()) {
            int rc = submitter.invoke(arena, payload);
            if (rc != 0) {
                throw new SubmitException(SubmitResult.fromValue(rc));
            }
        } finally {
            closeAll(payload);
        }
    }

    private void submitSpotReplySpot(RoutingId destNodeRid,
                                     RoutingId destSpotRid,
                                     long requestSeq,
                                     List<Message> payload) {
        for (int i = 0; i < payload.size(); i++) {
            int partFlag = i + 1 < payload.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = spotReplySpotPartOnce(destNodeRid, destSpotRid,
                    requestSeq, payload.get(i), partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == 4)
                    continue;
                throw submitFailure("zlink_spot_reply_spot_part");
            }
        }
    }

    private void submitSpotReplyRouter(RoutingId peerRid, long requestSeq,
                                       List<Message> payload) {
        for (int i = 0; i < payload.size(); i++) {
            int partFlag = i + 1 < payload.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            while (true) {
                int rc = spotReplyRouterPartOnce(peerRid, requestSeq,
                    payload.get(i), partFlag);
                if (rc == 0)
                    break;
                int errno = Native.errno();
                if (errno == 4)
                    continue;
                throw submitFailure("zlink_spot_reply_router_part");
            }
        }
    }

    private int spotReplySpotPartOnce(RoutingId destNodeRid,
                                      RoutingId destSpotRid,
                                      long requestSeq,
                                      Message part,
                                      int partFlag) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nodeRid = nativeRoutingId(arena, destNodeRid);
            MemorySegment spotRid = nativeRoutingId(arena, destSpotRid);
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            Object anchor = InternalAccess.messageTransferTo(part, nativeMsg);
            try {
                int rc = Native.spotReplySpotPart(handle(), nodeRid, spotRid,
                    requestSeq, nativeMsg, partFlag);
                if (rc != 0) {
                    InternalAccess.messageRestoreFromNative(part, nativeMsg,
                        false, anchor);
                }
                return rc;
            } catch (RuntimeException ex) {
                InternalAccess.messageRestoreFromNative(part, nativeMsg, false,
                    anchor);
                throw ex;
            }
        }
    }

    private int spotReplyRouterPartOnce(RoutingId peerRid,
                                        long requestSeq,
                                        Message part,
                                        int partFlag) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeRid = nativeRoutingId(arena, peerRid);
            MemorySegment nativeMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
            Object anchor = InternalAccess.messageTransferTo(part, nativeMsg);
            try {
                int rc = Native.spotReplyRouterPart(handle(), nativeRid,
                    requestSeq, nativeMsg, partFlag);
                if (rc != 0) {
                    InternalAccess.messageRestoreFromNative(part, nativeMsg,
                        false, anchor);
                }
                return rc;
            } catch (RuntimeException ex) {
                InternalAccess.messageRestoreFromNative(part, nativeMsg, false,
                    anchor);
                throw ex;
            }
        }
    }

    private SubmitException submitFailure(String apiName) {
        int errno = Native.errno();
        if (errno == 11 || errno == 10035) {
            return new SubmitException(SubmitResult.BACKPRESSURED, errno);
        }
        if (errno == 107 || errno == 10057 || errno == 113 || errno == 10065) {
            return new SubmitException(SubmitResult.NOT_CONNECTED, errno);
        }
        throw ZlinkException.fromLastError(apiName);
    }

    private final class SpotReceiveCursor implements Received.PartCursor {
        private final int flags;
        private final Arena arena = Arena.ofConfined();
        private final MemorySegment sourceRidOut = arena.allocate(
            ValueLayout.ADDRESS);
        private final MemorySegment spotRidOut = arena.allocate(
            ValueLayout.ADDRESS);
        private final MemorySegment requestSeqOut = arena.allocate(
            ValueLayout.JAVA_LONG);
        private final MemorySegment hasMoreOut = arena.allocate(
            ValueLayout.JAVA_INT);
        private boolean hasMore = true;
        private boolean closed;

        private SpotReceiveCursor(int flags) {
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
                    int rc = Native.spotRecvPart(handle(), sourceRidOut,
                        spotRidOut, requestSeqOut,
                        InternalAccess.messageNativeHandle(next),
                        hasMoreOut, flags);
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
                if (errno == 4)
                    continue;
                closeArena();
                throw ZlinkException.fromLastError("zlink_spot_recv_part");
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

    private MemorySegment handle() {
        return InternalAccess.spotHandle(spot);
    }

    private void ensureOpen() {
        MemorySegment handle = handle();
        if (handle == null || handle.address() == 0) {
            throw new IllegalStateException("spot is closed");
        }
        ensureNoCallbackFailure();
    }

    private void ensureNoCallbackFailure() {
        RuntimeException failure = callbackFailure;
        if (failure != null) {
            throw failure;
        }
    }

    private ExecutorService ensureCallbackExecutor(String threadName) {
        ExecutorService executor = callbackExecutor;
        if (executor != null) {
            return executor;
        }
        executor = Executors.newSingleThreadExecutor(runnable -> {
            Thread thread = new Thread(runnable, threadName);
            thread.setDaemon(true);
            return thread;
        });
        callbackExecutor = executor;
        return executor;
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

    private static CompletableFuture<Received> registerPending(long requestId,
                                                               long timeoutMs) {
        CompletableFuture<Received> future = new CompletableFuture<>();
        PENDING.put(requestId, future);
        ScheduledFuture<?> timeout = REQUEST_TIMEOUTS.schedule(() -> {
            if (PENDING.remove(requestId, future)) {
                future.completeExceptionally(new TimeoutException(
                    "request timed out"));
            }
        }, timeoutMs, TimeUnit.MILLISECONDS);
        future.whenComplete((ignored, error) -> timeout.cancel(false));
        return future;
    }

    private static void handleReplyCallback(int result, MemorySegment parts,
                                            long partCount,
                                            MemorySegment userdata) {
        long requestId = userdata.address();
        CompletableFuture<Received> future = PENDING.remove(requestId);
        try {
            if (result != RequestResult.OK.value()) {
                if (future != null) {
                    future.completeExceptionally(new RequestException(
                        RequestResult.fromValue(result), result));
                }
                return;
            }
            Message[] frames = InternalAccess.messageFromOwnedMsgVectorShared(
              parts, partCount);
            Received received = InternalAccess.received(null, null, frames, 0L,
              false, null);
            if (future == null || !future.complete(received)) {
                received.close();
            }
        } catch (Throwable error) {
            if (future != null) {
                future.completeExceptionally(error);
            }
        } finally {
            NativeMsg.multipartClose(parts, partCount);
        }
    }

    private static List<Message> clonePayload(List<Message> parts) {
        Objects.requireNonNull(parts, "parts");
        if (parts.isEmpty()) {
            throw new IllegalArgumentException("parts must not be empty");
        }
        Message[] copies = new Message[parts.size()];
        for (int i = 0; i < copies.length; i++) {
            copies[i] = InternalAccess.messageSharedCopyOf(parts.get(i));
        }
        return List.of(copies);
    }

    private static void closeAll(List<Message> parts) {
        for (Message part : parts) {
            try {
                part.close();
            } catch (RuntimeException ignored) {
            }
        }
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

    private static void requireReplyFlagsSupported(SendFlags flags) {
        Objects.requireNonNull(flags, "flags");
        if (flags != SendFlags.NONE) {
            throw new SubmitException(SubmitResult.NOT_SUPPORTED);
        }
    }

    private static RoutingId readRoutingId(MemorySegment nativeRid) {
        if (nativeRid == null || nativeRid.address() == 0) {
            return null;
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

    private static RoutingId readRoutingIdOut(MemorySegment nativeRidOut) {
        MemorySegment nativeRid = nativeRidOut.get(ValueLayout.ADDRESS, 0);
        if (nativeRid.address() != 0) {
            nativeRid = nativeRid.reinterpret(
              NativeLayouts.ROUTING_ID_LAYOUT.byteSize());
        }
        return readRoutingId(nativeRid);
    }

    private static long timeoutMillis(Duration timeout) {
        return timeout == null ? 5_000L : Math.max(1L, timeout.toMillis());
    }

    private static int toTimeoutInt(long timeoutMs) {
        if (timeoutMs <= 1L) {
            return 1;
        }
        return timeoutMs >= Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) timeoutMs;
    }

    private static Throwable unwrap(Throwable error) {
        if (error instanceof java.util.concurrent.CompletionException
            && error.getCause() != null) {
            return error.getCause();
        }
        return error;
    }

    private static RequestResult requestResult(Throwable error) {
        Throwable cause = unwrap(error);
        if (cause instanceof RequestException requestException) {
            return requestException.getResult();
        }
        if (cause instanceof TimeoutException) {
            return RequestResult.TIMED_OUT;
        }
        return RequestResult.PROTOCOL_ERROR;
    }

    private static MethodHandle callbackHandle(String name, MethodType type) {
        try {
            return MethodHandles.lookup().findStatic(SpotRoutedSupport.class,
              name, type);
        } catch (ReflectiveOperationException ex) {
            throw new ExceptionInInitializerError(ex);
        }
    }

    private MethodHandle callbackHandle(String name, MethodType type,
                                        SpotRoutedSupport receiver) {
        try {
            return MethodHandles.lookup().findVirtual(SpotRoutedSupport.class,
              name, type).bindTo(receiver);
        } catch (ReflectiveOperationException ex) {
            throw new ExceptionInInitializerError(ex);
        }
    }

    private static void closeArena(Arena arena) {
        if (arena != null && arena.scope().isAlive()) {
            arena.close();
        }
    }

    private static void shutdownExecutor(ExecutorService executor) {
        if (executor != null) {
            executor.shutdown();
        }
    }

    @FunctionalInterface
    private interface NativeRequest {
        int invoke(Arena arena, List<Message> payload, long requestId,
                   long timeoutMs);
    }

    @FunctionalInterface
    private interface NativeSubmit {
        int invoke(Arena arena, List<Message> payload);
    }

    private static final class TimeoutThreadFactory implements ThreadFactory {
        @Override
        public Thread newThread(Runnable runnable) {
            Thread thread = new Thread(runnable, "zlink-spot-request-timeout");
            thread.setDaemon(true);
            return thread;
        }
    }

    private record RoutedSnapshot(RoutingId sourceRid, RoutingId spotRid,
                                  long requestSeq, Message[] parts) {
    }
}
