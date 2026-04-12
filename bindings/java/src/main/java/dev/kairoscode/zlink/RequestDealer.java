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
import java.util.function.BiConsumer;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicLong;

public final class RequestDealer implements AutoCloseable {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_REPLY_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);
    private static final Arena CALLBACK_ARENA = Arena.ofShared();
    private static final MemorySegment REPLY_CALLBACK = LINKER.upcallStub(
      callbackHandle("handleReplyCallback", MethodType.methodType(void.class,
        int.class, MemorySegment.class, long.class, MemorySegment.class)),
      FD_REPLY_CALLBACK, CALLBACK_ARENA);
    private static final AtomicLong NEXT_REQUEST_ID = new AtomicLong(1L);
    private static final ConcurrentMap<Long, CompletableFuture<Received>> PENDING =
      new ConcurrentHashMap<>();
    private static final long RECV_POLL_SLEEP_MS = 1L;
    private static final boolean DEBUG_REQREP =
      Boolean.getBoolean("zlink.reqrep.debug");
    private static final Object CLOSED = new Object();

    private final DealerSocket socket;
    private final LinkedBlockingQueue<Object> dataQueue = new LinkedBlockingQueue<>();
    private final Thread dispatchThread;
    private volatile SocketMessageHandler dataHandler;
    private volatile boolean dispatchStarted;
    private volatile boolean closed;

    public RequestDealer(DealerSocket socket) {
        this.socket = Objects.requireNonNull(socket, "socket");
        this.dispatchThread = new Thread(this::dispatchLoop, "zlink-request-dealer-dispatch");
        this.dispatchThread.setDaemon(true);
        RoutingId rid = socket.routingId();
        debug("construct routingId=" + (rid == null ? "null" : rid.size()));
    }

    public DealerSocket socket() {
        return socket;
    }

    public CompletableFuture<Received> request(Message part) {
        return request(List.of(part));
    }

    public CompletableFuture<Received> request(List<Message> parts) {
        return request(parts, Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }

    public void request(Message part, BiConsumer<RequestResult, Received> callback) {
        request(List.of(part), callback);
    }

    public void request(List<Message> parts,
                        BiConsumer<RequestResult, Received> callback) {
        request(parts, callback, SendFlags.NONE,
            Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }

    public void request(Message part, BiConsumer<RequestResult, Received> callback,
                        SendFlags flags) {
        request(List.of(part), callback, flags);
    }

    public void request(List<Message> parts, BiConsumer<RequestResult, Received> callback,
                        SendFlags flags) {
        request(parts, callback, flags, Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }

    public void request(Message part, BiConsumer<RequestResult, Received> callback,
                        SendFlags flags, Duration timeout) {
        request(List.of(part), callback, flags, timeout);
    }

    public void request(List<Message> parts, BiConsumer<RequestResult, Received> callback,
                        SendFlags flags, Duration timeout) {
        Objects.requireNonNull(callback, "callback");
        RoutingId rid = socket.routingId();
        debug("request submit partCount=" + parts.size() + " routingId="
          + (rid == null ? "null" : rid.size()));
        requestInternal(parts, timeout, flags).whenComplete((reply, error) -> {
            RequestResult result = error == null ? RequestResult.OK
                : RequestReplySupport.requestResult(error);
            debug("callback result=" + result + " reply="
              + (reply == null ? "null" : reply.parts().size()));
            callback.accept(result, reply);
        });
    }

    public CompletableFuture<Received> request(Message part, Duration timeout) {
        return request(List.of(part), timeout);
    }

    public CompletableFuture<Received> request(List<Message> parts, Duration timeout) {
        return requestInternal(parts, timeout, SendFlags.NONE);
    }

    public CompletableFuture<Received> request(Message part, Duration timeout,
                                                SendFlags flags) {
        return requestInternal(List.of(part), timeout, flags);
    }

    private CompletableFuture<Received> requestInternal(List<Message> parts,
                                                         Duration timeout,
                                                         SendFlags flags) {
        Objects.requireNonNull(parts, "parts");
        List<Message> payload = RequestReplySupport.clonePayload(parts);
        long requestId = NEXT_REQUEST_ID.getAndIncrement();
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        CompletableFuture<Received> future = registerPending(requestId, timeoutMs);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeParts = movePayloadToNative(arena, payload);
            int rc = NativeMsg.dealerRequest(socket.handle(), nativeParts,
              payload.size(), toTimeoutInt(timeoutMs), REPLY_CALLBACK,
              MemorySegment.ofAddress(requestId), flags == null ? 0 : flags.value());
            debug("dealerRequest rc=" + rc + " requestId=" + requestId);
            if (rc != 0) {
                RequestReplySupport.closeAll(payload);
                throw new SubmitException(SubmitResult.fromValue(rc));
            }
        } catch (Throwable error) {
            PENDING.remove(requestId);
            RequestReplySupport.closeAll(payload);
            if (error instanceof SubmitException submitException) {
                throw submitException;
            }
            throw error instanceof RuntimeException runtimeException
                ? runtimeException
                : new IllegalStateException("request submission failed", error);
        }
        return future;
    }

    public Received recv() {
        return recv(RecvFlags.NONE);
    }

    public Received recv(RecvFlags flags) {
        Objects.requireNonNull(flags, "flags");
        if (dataHandler != null) {
            throw new IllegalStateException("socket is in callback mode; direct recv is not allowed");
        }
        if (flags == RecvFlags.DONT_WAIT) {
            Optional<Received> maybe = tryRecv();
            if (maybe.isPresent()) {
                return maybe.get();
            }
            throw new RecvException(RecvResult.NO_DATA);
        }
        try {
            Object item = dataQueue.take();
            if (item == CLOSED) {
                throw new IllegalStateException("request dealer is closed");
            }
            return (Received) item;
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("recv interrupted", ex);
        }
    }

    Optional<Received> tryRecv() {
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
        startDispatchIfNeeded();
    }

    @Override
    public void close() {
        if (closed) {
            return;
        }
        closed = true;
        dataQueue.offer(CLOSED);
        socket.close();
        if (dispatchStarted) {
            dispatchThread.interrupt();
        }
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
                if (dataHandler == null) {
                    Thread.sleep(RECV_POLL_SLEEP_MS);
                    continue;
                }
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
                throw new IllegalStateException("request dealer dispatch interrupted", ex);
            } catch (RuntimeException ex) {
                if (closed) {
                    return;
                }
                throw ex;
            }
        }
    }

    private void startDispatchIfNeeded() {
        if (dispatchStarted) {
            return;
        }
        synchronized (this) {
            if (dispatchStarted) {
                return;
            }
            dispatchStarted = true;
            dispatchThread.start();
        }
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

    private static int toTimeoutInt(long timeoutMs) {
        if (timeoutMs <= 1L) {
            return 1;
        }
        return timeoutMs >= Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) timeoutMs;
    }

    private static byte[][] snapshotParts(MemorySegment parts, long partCount) {
        int count = Math.toIntExact(partCount);
        if (count == 0 || parts == null || parts.address() == 0) {
            return new byte[0][];
        }
        long msgSize = NativeLayouts.MSG_LAYOUT.byteSize();
        MemorySegment vector = MemorySegment.ofAddress(parts.address()).reinterpret(
            msgSize * count);
        byte[][] snapshot = new byte[count][];
        for (int i = 0; i < count; i++) {
            MemorySegment src = vector.asSlice((long) i * msgSize, msgSize);
            int size = Math.toIntExact(NativeMsg.msgSize(src));
            byte[] bytes = new byte[size];
            if (size > 0) {
                MemorySegment data = NativeMsg.msgData(src).reinterpret(size);
                MemorySegment.copy(data, 0, MemorySegment.ofArray(bytes), 0, size);
            }
            snapshot[i] = bytes;
        }
        return snapshot;
    }

    private static Message[] materializeSnapshot(byte[][] snapshot) {
        Message[] parts = new Message[snapshot.length];
        for (int i = 0; i < snapshot.length; i++) {
            parts[i] = Message.sharedCopyOf(snapshot[i]);
        }
        return parts;
    }

    private static void handleReplyCallback(int errno,
                                            MemorySegment parts,
                                            long partCount,
                                            MemorySegment userData) {
        long requestId = userData.address();
        CompletableFuture<Received> future = PENDING.remove(requestId);
        try {
            if (errno != 0) {
                if (future != null) {
                    future.completeExceptionally(new RequestException(
                        requestResult(errno), errno));
                }
                return;
            }
            Received received = new Received(null,
                materializeSnapshot(snapshotParts(parts, partCount)), true);
            if (future == null || !future.complete(received)) {
                received.close();
            }
        } catch (Throwable error) {
            if (future != null) {
                future.completeExceptionally(error);
            }
        }
    }

    private static RequestResult requestResult(int errno) {
        try {
            return RequestResult.fromValue(errno);
        } catch (IllegalArgumentException ignored) {
            return RequestResult.PROTOCOL_ERROR;
        }
    }

    private static void debug(String message) {
        if (DEBUG_REQREP) {
            try {
                Files.writeString(Path.of("/tmp/zlink-reqrep.log"),
                    "[dealer] " + message + System.lineSeparator(),
                    StandardOpenOption.CREATE, StandardOpenOption.APPEND);
            } catch (Exception ignored) {
            }
        }
    }

    private static MethodHandle callbackHandle(String name, MethodType type) {
        try {
            return MethodHandles.lookup().findStatic(RequestDealer.class, name, type);
        } catch (ReflectiveOperationException ex) {
            throw new ExceptionInInitializerError(ex);
        }
    }
}
