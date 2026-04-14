/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeRequestReplyBridge;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.function.BiConsumer;

final class DealerRequestSupport implements AutoCloseable {
    private static final long RECV_POLL_SLEEP_MS = 1L;
    private static final Object CLOSED = new Object();

    private final DealerSocket socket;
    private final boolean closeSocketOnClose;
    private final LinkedBlockingQueue<Object> dataQueue = new LinkedBlockingQueue<>();
    private final Thread dispatchThread;
    private volatile SocketMessageHandler dataHandler;
    private volatile boolean dispatchStarted;
    private volatile boolean closed;

    DealerRequestSupport(DealerSocket socket) {
        this(socket, true);
    }

    DealerRequestSupport(DealerSocket socket, boolean closeSocketOnClose) {
        this.socket = Objects.requireNonNull(socket, "socket");
        this.closeSocketOnClose = closeSocketOnClose;
        this.dispatchThread = new Thread(this::dispatchLoop,
            "zlink-request-dealer-dispatch");
        this.dispatchThread.setDaemon(true);
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
        request(parts, callback, flags,
            Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }

    public void request(Message part, BiConsumer<RequestResult, Received> callback,
                        SendFlags flags, Duration timeout) {
        request(List.of(part), callback, flags, timeout);
    }

    public void request(List<Message> parts, BiConsumer<RequestResult, Received> callback,
                        SendFlags flags, Duration timeout) {
        Objects.requireNonNull(callback, "callback");
        requestInternal(parts, timeout, flags).whenComplete((reply, error) -> {
            callback.accept(error == null ? RequestResult.OK
                : RequestReplySupport.requestResult(error), reply);
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
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        return RequestReplySupport.startRequestExecution(
            () -> submitRequest(payload, timeoutMs, flags));
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
            return tryRecv().orElseThrow(() -> new RecvException(RecvResult.NO_DATA));
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
            throw new IllegalStateException(
                "socket is in callback mode; direct recv is not allowed");
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
        dataQueue.offer(CLOSED);
        if (dispatchStarted) {
            dispatchThread.interrupt();
            try {
                dispatchThread.join(TimeUnit.SECONDS.toMillis(1));
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
            }
        }
    }

    void finishClose() {
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
                throw new IllegalStateException("request dealer dispatch interrupted",
                    ex);
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

    private Received submitRequest(List<Message> payload,
                                   long timeoutMs,
                                   SendFlags flags) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeParts = movePayloadToNative(arena, payload);
            NativeRequestReplyBridge.ReplyResult reply =
                NativeRequestReplyBridge.dealerRequestSync(
                    socket.handle(), nativeParts, payload.size(),
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
            return new Received(null,
                Message.fromMsgVector(reply.replyParts(), reply.replyPartCount()),
                true);
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
}
