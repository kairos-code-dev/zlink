/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.RequestProgressPump;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Supplier;

final class RequestReplySupport {
    static final long DEFAULT_TIMEOUT_MS = 5_000L;
    private static final ScheduledExecutorService REQUEST_TIMEOUTS =
      Executors.newSingleThreadScheduledExecutor(
        new NamedDaemonThreadFactory("zlink-request-timeout"));
    private static final java.util.concurrent.ExecutorService REQUEST_COMPLETIONS =
      Executors.newSingleThreadExecutor(
        new NamedDaemonThreadFactory("zlink-request-complete"));

    private RequestReplySupport() {
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

    static Message cloneMessage(Message source) {
        return Message.sharedCopyOf(source);
    }

    static List<Message> takeReceivedParts(Received received) {
        return received.takeParts();
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
        try {
            return CompletableFuture.completedFuture(supplier.get());
        } catch (Throwable error) {
            return CompletableFuture.failedFuture(error);
        }
    }

    static <T> CompletableFuture<T> startTimedRequestExecution(
            Supplier<T> supplier,
            long timeoutMs) {
        if (timeoutMs < 0L)
            throw new IllegalArgumentException("timeoutMs must be >= 0");
        return startRequestExecution(supplier);
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

    static void startSocketRequestProgress(CompletableFuture<?> future,
                                           MemorySegment socketHandle,
                                           String threadName) {
        RequestProgressPump.trackSocketRequest(future, socketHandle, threadName);
    }

    @FunctionalInterface
    interface ThrowingSendNoWaitResult {
        SendResult sendNoWaitResult(List<Message> parts);
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
