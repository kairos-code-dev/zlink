/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.Executor;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Supplier;

final class RequestReplySupport {
    static final long DEFAULT_TIMEOUT_MS = 5_000L;
    private static final long SEND_RETRY_SLEEP_MS = 1L;
    private static final ScheduledExecutorService REQUEST_TIMEOUTS =
      Executors.newSingleThreadScheduledExecutor(
        new NamedDaemonThreadFactory("zlink-request-timeout"));
    private static final ExecutorService REQUEST_EXECUTIONS =
      Executors.newCachedThreadPool(
        new NamedDaemonThreadFactory("zlink-request-exec"));
    private static final ExecutorService REQUEST_COMPLETIONS =
      Executors.newSingleThreadExecutor(
        new NamedDaemonThreadFactory("zlink-request-complete"));

    private RequestReplySupport() {
    }

    static CompletableFuture<Void> asyncSend(List<Message> payload,
                                             long timeoutMs,
                                             ThrowingTrySend sender) {
        return CompletableFuture.runAsync(() -> {
            long deadline = System.nanoTime() + TimeUnit.MILLISECONDS.toNanos(timeoutMs);
            while (true) {
                SendResult result = sender.trySend(payload);
                if (result == SendResult.SENT) {
                    return;
                }
                if (System.nanoTime() >= deadline) {
                    closeAll(payload);
                    throw new CompletionException(new TimeoutException("request timed out"));
                }
                try {
                    Thread.sleep(SEND_RETRY_SLEEP_MS);
                } catch (InterruptedException ex) {
                    Thread.currentThread().interrupt();
                    closeAll(payload);
                    throw new CompletionException(ex);
                }
            }
        }, REQUEST_EXECUTIONS);
    }

    static long timeoutMillis(Duration timeout) {
        return timeout == null ? DEFAULT_TIMEOUT_MS : Math.max(1L, timeout.toMillis());
    }

    static <T> void armTimeout(ConcurrentMap<Long, CompletableFuture<T>> pending,
                               long requestId,
                               CompletableFuture<T> future,
                               long timeoutMs) {
        ScheduledFuture<?> timeout = REQUEST_TIMEOUTS.schedule(() -> {
            if (pending.remove(requestId, future)) {
                future.completeExceptionally(new RequestException(
                    RequestResult.TIMED_OUT));
            }
        }, timeoutMs, TimeUnit.MILLISECONDS);
        future.whenComplete((ignored, error) -> timeout.cancel(false));
    }

    static Received cloneReceived(Received received) {
        Message[] parts = new Message[received.parts().size()];
        for (int i = 0; i < parts.length; i++) {
            parts[i] = cloneMessage(received.parts().get(i));
        }
        RoutingId routingId = received.routingId()
            .map(rid -> RoutingId.fromBytes(rid.toBytes()))
            .orElse(null);
        RoutingId spotRid = received.spotRid()
            .map(rid -> RoutingId.fromBytes(rid.toBytes()))
            .orElse(null);
        return new Received(routingId, spotRid, parts, true,
            received.requestSeq().orElse(0L),
            received.requestSeq().isPresent(), null);
    }

    static List<Message> clonePayload(List<Message> parts) {
        Objects.requireNonNull(parts, "parts");
        if (parts.isEmpty()) {
            throw new IllegalArgumentException("parts must not be empty");
        }
        Message[] copies = new Message[parts.size()];
        for (int i = 0; i < copies.length; i++) {
            copies[i] = cloneMessage(parts.get(i));
        }
        return List.of(copies);
    }

    static Message cloneMessage(Message source) {
        return Message.sharedCopyOf(source);
    }

    static void closeAll(List<Message> parts) {
        for (Message part : parts) {
            try {
                part.close();
            } catch (RuntimeException ignored) {
            }
        }
    }

    static Throwable unwrap(Throwable error) {
        if (error instanceof CompletionException && error.getCause() != null) {
            return error.getCause();
        }
        return error;
    }

    static RequestResult requestResult(Throwable error) {
        Throwable cause = unwrap(error);
        if (cause instanceof RequestException requestException) {
            return requestException.getResult();
        }
        if (cause instanceof java.util.concurrent.TimeoutException) {
            return RequestResult.TIMED_OUT;
        }
        return RequestResult.PROTOCOL_ERROR;
    }

    static SubmitResult submitResult(Throwable error) {
        Throwable cause = unwrap(error);
        if (cause instanceof SubmitException submitException) {
            return submitException.getResult();
        }
        return SubmitResult.INTERNAL_ERROR;
    }

    static Throwable normalizeRequestFailure(Throwable error) {
        Throwable cause = unwrap(error);
        if (cause instanceof RequestException
            || cause instanceof SubmitException
            || cause instanceof RuntimeException) {
            return cause;
        }
        return new RequestException(RequestResult.PROTOCOL_ERROR);
    }

    static RequestException requestFailure(Throwable error) {
        Throwable cause = unwrap(error);
        if (cause instanceof RequestException requestException) {
            return requestException;
        }
        if (cause instanceof RecvException recvException) {
            if (recvException.getResult() == RecvResult.TERMINATED) {
                return new RequestException(RequestResult.TERMINATED,
                    recvException.getInternalErrno());
            }
            return new RequestException(RequestResult.PROTOCOL_ERROR,
                recvException.getInternalErrno());
        }
        if (cause instanceof ZlinkException zlinkException) {
            return new RequestException(RequestResult.PROTOCOL_ERROR,
                zlinkException.getInternalErrno());
        }
        return new RequestException(RequestResult.PROTOCOL_ERROR);
    }

    static void requireReplyFlagsSupported(SendFlags flags) {
        Objects.requireNonNull(flags, "flags");
        if (flags != SendFlags.NONE) {
            throw new SubmitException(SubmitResult.NOT_SUPPORTED);
        }
    }

    static boolean isClosedSignal(IllegalStateException ex) {
        String message = ex.getMessage();
        return message != null && message.contains("closed");
    }

    static Executor callbackCompletions() {
        return REQUEST_COMPLETIONS;
    }

    static <T> CompletableFuture<T> startRequestExecution(Supplier<T> supplier) {
        return CompletableFuture.supplyAsync(() -> supplier.get(),
            REQUEST_EXECUTIONS);
    }

    static <T> void completeAsync(CompletableFuture<T> future,
                                  Supplier<T> supplier) {
        callbackCompletions().execute(() -> {
            if (future == null || future.isDone()) {
                return;
            }
            try {
                T value = supplier.get();
                if (!future.complete(value) && value instanceof AutoCloseable closeable) {
                    try {
                        closeable.close();
                    } catch (Exception ignored) {
                    }
                }
            } catch (Throwable error) {
                future.completeExceptionally(error);
            }
        });
    }

    static void completeExceptionallyAsync(CompletableFuture<?> future,
                                           Throwable error) {
        callbackCompletions().execute(() -> {
            if (future != null) {
                future.completeExceptionally(error);
            }
        });
    }

    @FunctionalInterface
    interface ThrowingTrySend {
        SendResult trySend(List<Message> parts);
    }

    private static final class NamedDaemonThreadFactory implements ThreadFactory {
        private final String name;
        private final AtomicInteger sequence = new AtomicInteger();

        private NamedDaemonThreadFactory(String name) {
            this.name = name;
        }

        @Override
        public Thread newThread(Runnable runnable) {
            Thread thread = new Thread(runnable,
                name + "-" + sequence.getAndIncrement());
            thread.setDaemon(true);
            return thread;
        }
    }

}
