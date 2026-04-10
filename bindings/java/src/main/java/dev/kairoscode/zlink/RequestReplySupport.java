/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

final class RequestReplySupport {
    static final long DEFAULT_TIMEOUT_MS = 5_000L;
    private static final long SEND_RETRY_SLEEP_MS = 1L;

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
        });
    }

    static long timeoutMillis(Duration timeout) {
        return timeout == null ? DEFAULT_TIMEOUT_MS : Math.max(1L, timeout.toMillis());
    }

    static Received cloneReceived(Received received) {
        Message[] parts = new Message[received.parts().size()];
        for (int i = 0; i < parts.length; i++) {
            parts[i] = cloneMessage(received.parts().get(i));
        }
        RoutingId routingId = received.hasRoutingId()
            ? RoutingId.copyOf(received.routingId().toByteArray())
            : null;
        return new Received(routingId, parts, true, received.requestSequence(),
            received.hasRequestSequence());
    }

    static List<Message> clonePayload(List<Message> parts) {
        Objects.requireNonNull(parts, "parts");
        if (parts.isEmpty()) {
            throw new IllegalArgumentException("parts must not be empty");
        }
        return parts.stream().map(RequestReplySupport::cloneMessage).toList();
    }

    static Message cloneMessage(Message source) {
        return Message.sharedCopyOf(source.toByteArray());
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

    static boolean isClosedSignal(IllegalStateException ex) {
        String message = ex.getMessage();
        return message != null && message.contains("closed");
    }

    @FunctionalInterface
    interface ThrowingTrySend {
        SendResult trySend(List<Message> parts);
    }
}
