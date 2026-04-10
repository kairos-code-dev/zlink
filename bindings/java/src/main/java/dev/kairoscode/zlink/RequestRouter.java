/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

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
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicLong;

public final class RequestRouter implements AutoCloseable {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_REPLY_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_REQUEST_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
        ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);
    private static final Arena CALLBACK_ARENA = Arena.ofShared();
    private static final MemorySegment REPLY_CALLBACK = LINKER.upcallStub(
      callbackHandle("handleReplyCallback", MethodType.methodType(void.class,
        int.class, MemorySegment.class, long.class, MemorySegment.class)),
      FD_REPLY_CALLBACK, CALLBACK_ARENA);
    private static final MemorySegment REQUEST_CALLBACK = LINKER.upcallStub(
      callbackHandle("handleRequestCallback", MethodType.methodType(void.class,
        MemorySegment.class, long.class, MemorySegment.class, long.class,
        MemorySegment.class)),
      FD_REQUEST_CALLBACK, CALLBACK_ARENA);
    private static final AtomicLong NEXT_REQUEST_ID = new AtomicLong(1L);
    private static final AtomicLong NEXT_ROUTER_ID = new AtomicLong(1L);
    private static final ConcurrentMap<Long, CompletableFuture<Received>> PENDING =
      new ConcurrentHashMap<>();
    private static final ConcurrentMap<Long, RequestRouter> ROUTERS =
      new ConcurrentHashMap<>();
    private static final long RECV_POLL_SLEEP_MS = 1L;
    private static final Object CLOSED = new Object();

    private final RouterSocket socket;
    private final long routerId;
    private final LinkedBlockingQueue<Object> dataQueue = new LinkedBlockingQueue<>();
    private final Thread dispatchThread;
    private volatile SocketMessageHandler dataHandler;
    private volatile boolean closed;

    public RequestRouter(RouterSocket socket) {
        this.socket = Objects.requireNonNull(socket, "socket");
        this.routerId = NEXT_ROUTER_ID.getAndIncrement();
        ROUTERS.put(routerId, this);
        int rc = NativeMsg.routerHandler(socket.handle(), REQUEST_CALLBACK,
          MemorySegment.ofAddress(routerId));
        if (rc != 0) {
            ROUTERS.remove(routerId);
            throw ZlinkException.fromLastError("zlink_router_handler");
        }
        this.dispatchThread = new Thread(this::dispatchLoop, "zlink-request-router-dispatch");
        this.dispatchThread.setDaemon(true);
        this.dispatchThread.start();
    }

    public RouterSocket socket() {
        return socket;
    }

    public CompletableFuture<Received> request(RoutingId routingId,
                                               Message part,
                                               Duration timeout) {
        return request(routingId, List.of(part), timeout);
    }

    public CompletableFuture<Received> request(RoutingId routingId,
                                               List<Message> parts,
                                               Duration timeout) {
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(parts, "parts");
        List<Message> payload = RequestReplySupport.clonePayload(parts);
        long requestId = NEXT_REQUEST_ID.getAndIncrement();
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        CompletableFuture<Received> future = registerPending(requestId, timeoutMs);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeRid = nativeRoutingId(arena, routingId);
            MemorySegment nativeParts = movePayloadToNative(arena, payload);
            int rc = NativeMsg.routerRequest(socket.handle(), nativeRid, nativeParts,
              payload.size(), toTimeoutInt(timeoutMs), REPLY_CALLBACK,
              MemorySegment.ofAddress(requestId));
            if (rc != 0) {
                PENDING.remove(requestId);
                future.completeExceptionally(
                  ZlinkException.fromLastError("zlink_router_request"));
            }
        } catch (Throwable error) {
            PENDING.remove(requestId);
            future.completeExceptionally(RequestReplySupport.unwrap(error));
            RequestReplySupport.closeAll(payload);
        }
        return future;
    }

    public CompletableFuture<Received> tryRequest(RoutingId routingId,
                                                  List<Message> parts,
                                                  Duration timeout) {
        return request(routingId, parts, timeout);
    }

    public void request(RoutingId routingId,
                        List<Message> parts,
                        Duration timeout,
                        RequestReplyCallback callback) {
        Objects.requireNonNull(callback, "callback");
        request(routingId, parts, timeout)
          .whenComplete((reply, error) -> callback.onComplete(
            RequestReplySupport.unwrap(error), reply));
    }

    public CompletableFuture<Void> reply(RoutingId routingId,
                                         long requestSequence,
                                         Message part) {
        return reply(routingId, requestSequence, List.of(part));
    }

    public CompletableFuture<Void> reply(RoutingId routingId,
                                         long requestSequence,
                                         List<Message> parts) {
        return CompletableFuture.runAsync(() -> {
            SendResult result = tryReply(routingId, requestSequence, parts);
            if (result != SendResult.SENT) {
                throw new IllegalStateException("reply send is backpressured");
            }
        });
    }

    public SendResult tryReply(RoutingId routingId,
                               long requestSequence,
                               List<Message> parts) {
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(parts, "parts");
        List<Message> payload = RequestReplySupport.clonePayload(parts);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeRid = nativeRoutingId(arena, routingId);
            MemorySegment nativeParts = movePayloadToNative(arena, payload);
            int rc = NativeMsg.routerReply(socket.handle(), nativeRid, requestSequence,
              nativeParts, payload.size());
            if (rc == 0) {
                return SendResult.SENT;
            }
            int errno = ZlinkException.fromLastError("zlink_router_reply").errno();
            if (errno == Socket.ERRNO_EAGAIN || errno == Socket.ERRNO_EWOULDBLOCK_WIN) {
                return SendResult.BACKPRESSURED;
            }
            throw ZlinkException.fromErrno("zlink_router_reply", errno);
        } catch (RuntimeException error) {
            RequestReplySupport.closeAll(payload);
            throw error;
        }
    }

    public Received recv() {
        if (dataHandler != null) {
            throw new IllegalStateException("socket is in callback mode; direct recv is not allowed");
        }
        try {
            Object item = dataQueue.take();
            if (item == CLOSED) {
                throw new IllegalStateException("request router is closed");
            }
            return (Received) item;
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("recv interrupted", ex);
        }
    }

    public Optional<Received> tryRecv() {
        if (dataHandler != null) {
            throw new IllegalStateException("socket is in callback mode; direct recv is not allowed");
        }
        Object item = dataQueue.poll();
        if (item == null || item == CLOSED) {
            return Optional.empty();
        }
        return Optional.of((Received) item);
    }

    public void onReceive(SocketMessageHandler handler) {
        this.dataHandler = Objects.requireNonNull(handler, "handler");
    }

    @Override
    public void close() {
        if (closed) {
            return;
        }
        closed = true;
        ROUTERS.remove(routerId, this);
        dataQueue.offer(CLOSED);
        socket.close();
        dispatchThread.interrupt();
        try {
            dispatchThread.join(TimeUnit.SECONDS.toMillis(1));
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
        }
    }

    private void deliverData(Received received) {
        SocketMessageHandler handler = dataHandler;
        if (handler == null) {
            dataQueue.offer(received);
            return;
        }
        try (received) {
            handler.onMessage(received);
        }
    }

    private void dispatchLoop() {
        while (!closed) {
            try {
                Optional<Received> maybeReceived = socket.tryRecv();
                if (maybeReceived.isPresent()) {
                    try (Received received = maybeReceived.get()) {
                        deliverData(RequestReplySupport.cloneReceived(received));
                    }
                    continue;
                }
                Thread.sleep(RECV_POLL_SLEEP_MS);
            } catch (IllegalStateException ex) {
                if (closed || RequestReplySupport.isClosedSignal(ex)) {
                    return;
                }
                throw ex;
            } catch (InterruptedException ex) {
                if (closed) {
                    Thread.currentThread().interrupt();
                    return;
                }
                Thread.currentThread().interrupt();
                throw new IllegalStateException("request router dispatch interrupted", ex);
            } catch (RuntimeException ex) {
                if (closed) {
                    return;
                }
                throw ex;
            }
        }
    }

    private void handleIncomingRequest(MemorySegment peerRid,
                                       long requestSequence,
                                       MemorySegment parts,
                                       long partCount) {
        RoutingId routingId = peerRid == null || peerRid.address() == 0
          ? null : RoutingId.copyOf(readRoutingId(peerRid));
        Message[] requestParts = Message.fromOwnedMsgVector(parts, partCount);
        deliverData(new Received(routingId, requestParts, true, requestSequence, true));
    }

    private static CompletableFuture<Received> registerPending(long requestId,
                                                               long timeoutMs) {
        CompletableFuture<Received> future = new CompletableFuture<>();
        PENDING.put(requestId, future);
        future.orTimeout(timeoutMs, TimeUnit.MILLISECONDS)
          .whenComplete((ignored, error) -> {
              Throwable cause = RequestReplySupport.unwrap(error);
              if (cause instanceof TimeoutException) {
                  PENDING.remove(requestId, future);
              }
          });
        return future;
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

    private static byte[] readRoutingId(MemorySegment nativeRid) {
        int size = Byte.toUnsignedInt(nativeRid.get(ValueLayout.JAVA_BYTE,
          NativeLayouts.ROUTING_ID_SIZE_OFFSET));
        if (size == 0) {
            return new byte[0];
        }
        return nativeRid.asSlice(NativeLayouts.ROUTING_ID_DATA_OFFSET, size)
          .toArray(ValueLayout.JAVA_BYTE);
    }

    private static int toTimeoutInt(long timeoutMs) {
        if (timeoutMs <= 1L) {
            return 1;
        }
        return timeoutMs >= Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) timeoutMs;
    }

    private static void handleReplyCallback(int errno,
                                            MemorySegment parts,
                                            long partCount,
                                            MemorySegment userData) {
        long requestId = userData.address();
        CompletableFuture<Received> future = PENDING.remove(requestId);
        if (errno != 0) {
            if (future != null) {
                future.completeExceptionally(
                  ZlinkException.fromErrno("zlink_router_request", errno));
            } else if (parts != null && parts.address() != 0 && partCount > 0) {
                NativeMsg.msgvClose(parts, partCount);
            }
            return;
        }
        Message[] replyParts = Message.fromOwnedMsgVector(parts, partCount);
        Received received = new Received(null, replyParts, true);
        if (future == null || !future.complete(received)) {
            received.close();
        }
    }

    private static void handleRequestCallback(MemorySegment peerRid,
                                              long requestSequence,
                                              MemorySegment parts,
                                              long partCount,
                                              MemorySegment userData) {
        RequestRouter router = ROUTERS.get(userData.address());
        if (router == null) {
            if (parts != null && parts.address() != 0 && partCount > 0) {
                NativeMsg.msgvClose(parts, partCount);
            }
            return;
        }
        router.handleIncomingRequest(peerRid, requestSequence, parts, partCount);
    }

    private static MethodHandle callbackHandle(String name, MethodType type) {
        try {
            return MethodHandles.lookup().findStatic(RequestRouter.class, name, type);
        } catch (ReflectiveOperationException ex) {
            throw new ExceptionInInitializerError(ex);
        }
    }
}
