/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

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

public final class RequestDealer implements AutoCloseable {
    private static final long RECV_POLL_SLEEP_MS = 1L;
    private static final Object CLOSED = new Object();

    private final DealerSocket socket;
    private final AtomicLong nextCorrelationId = new AtomicLong(1L);
    private final ConcurrentMap<Long, CompletableFuture<Received>> pending =
        new ConcurrentHashMap<>();
    private final LinkedBlockingQueue<Object> dataQueue = new LinkedBlockingQueue<>();
    private final Thread dispatchThread;
    private volatile SocketMessageHandler dataHandler;
    private volatile boolean closed;

    public RequestDealer(DealerSocket socket) {
        this.socket = Objects.requireNonNull(socket, "socket");
        this.dispatchThread = new Thread(this::dispatchLoop, "zlink-request-dealer-dispatch");
        this.dispatchThread.setDaemon(true);
        this.dispatchThread.start();
    }

    public DealerSocket socket() {
        return socket;
    }

    public CompletableFuture<Received> request(Message part, Duration timeout) {
        return request(List.of(part), timeout);
    }

    public CompletableFuture<Received> request(List<Message> parts, Duration timeout) {
        long correlationId = nextCorrelationId.getAndIncrement();
        List<Message> payload = RequestReplySupport.clonePayload(parts);
        payload.getFirst().setRequest(correlationId);
        CompletableFuture<Received> future = registerPending(correlationId, timeout);
        RequestReplySupport.asyncSend(payload, RequestReplySupport.timeoutMillis(timeout),
            messages -> socket.trySend(messages))
            .whenComplete((ignored, error) -> {
                if (error != null) {
                    failPending(correlationId, RequestReplySupport.unwrap(error));
                }
            });
        return future;
    }

    public CompletableFuture<Received> tryRequest(Message part, Duration timeout) {
        return tryRequest(List.of(part), timeout);
    }

    public CompletableFuture<Received> tryRequest(List<Message> parts, Duration timeout) {
        long correlationId = nextCorrelationId.getAndIncrement();
        List<Message> payload = RequestReplySupport.clonePayload(parts);
        payload.getFirst().setRequest(correlationId);
        CompletableFuture<Received> future = registerPending(correlationId, timeout);
        try {
            SendResult result = socket.trySend(payload);
            if (result != SendResult.SENT) {
                RequestReplySupport.closeAll(payload);
                throw new IllegalStateException("request send is backpressured");
            }
        } catch (Throwable error) {
            failPending(correlationId, error);
        }
        return future;
    }

    public void request(List<Message> parts,
                        Duration timeout,
                        RequestReplyCallback callback) {
        Objects.requireNonNull(callback, "callback");
        request(parts, timeout)
            .whenComplete((reply, error) -> callback.onComplete(
                RequestReplySupport.unwrap(error), reply));
    }

    public Received recv() {
        if (dataHandler != null) {
            throw new IllegalStateException("socket is in callback mode; direct recv is not allowed");
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
        pending.forEach((id, future) ->
            future.completeExceptionally(new IllegalStateException("request dealer is closed")));
        pending.clear();
        dataQueue.offer(CLOSED);
        socket.close();
        dispatchThread.interrupt();
        try {
            dispatchThread.join(TimeUnit.SECONDS.toMillis(1));
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
        }
    }

    private void dispatch(Received received) {
        Received snapshot = RequestReplySupport.cloneReceived(received);
        long[] info = snapshot.firstPart().getRequestInfo();
        if (info[0] == Message.MSG_TYPE_REPLY) {
            CompletableFuture<Received> future = pending.remove(info[1]);
            if (future == null) {
                snapshot.close();
                return;
            }
            future.complete(snapshot);
            return;
        }
        deliverData(snapshot);
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
                        dispatch(received);
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

    private CompletableFuture<Received> registerPending(long correlationId, Duration timeout) {
        CompletableFuture<Received> future = new CompletableFuture<>();
        pending.put(correlationId, future);
        future.orTimeout(RequestReplySupport.timeoutMillis(timeout), TimeUnit.MILLISECONDS)
            .whenComplete((ignored, error) -> {
                Throwable cause = RequestReplySupport.unwrap(error);
                if (cause instanceof TimeoutException) {
                    pending.remove(correlationId);
                }
            });
        return future;
    }

    private void failPending(long correlationId, Throwable error) {
        CompletableFuture<Received> future = pending.remove(correlationId);
        if (future != null) {
            future.completeExceptionally(error);
        }
    }
}
