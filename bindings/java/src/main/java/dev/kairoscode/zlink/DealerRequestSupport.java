/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.function.BiConsumer;

final class DealerRequestSupport implements AutoCloseable {
    private final DealerSocket socket;
    private final boolean closeSocketOnClose;

    DealerRequestSupport(DealerSocket socket) {
        this(socket, true);
    }

    DealerRequestSupport(DealerSocket socket, boolean closeSocketOnClose) {
        this.socket = Objects.requireNonNull(socket, "socket");
        this.closeSocketOnClose = closeSocketOnClose;
    }

    public DealerSocket socket() {
        return socket;
    }

    public CompletableFuture<Received> request(Message part) {
        return request(List.of(part));
    }

    public CompletableFuture<Received> request(Message part, SendFlags flags) {
        return request(List.of(part), flags);
    }

    public CompletableFuture<Received> request(List<Message> parts) {
        return request(parts, SendFlags.NONE);
    }

    public CompletableFuture<Received> request(List<Message> parts,
                                               SendFlags flags) {
        return request(parts,
            Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS), flags);
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
        return request(parts, timeout, SendFlags.NONE);
    }

    public CompletableFuture<Received> request(Message part, Duration timeout,
                                               SendFlags flags) {
        return request(List.of(part), timeout, flags);
    }

    public CompletableFuture<Received> request(Message part, SendFlags flags,
                                               Duration timeout) {
        return request(List.of(part), timeout, flags);
    }

    public CompletableFuture<Received> request(List<Message> parts,
                                               Duration timeout,
                                               SendFlags flags) {
        return requestInternal(parts, timeout, flags);
    }

    private CompletableFuture<Received> requestInternal(List<Message> parts,
                                                        Duration timeout,
                                                        SendFlags flags) {
        Objects.requireNonNull(parts, "parts");
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        long requestId = RoutedRequestSupport.nextRequestId();
        CompletableFuture<Received> future =
            RoutedRequestSupport.registerPending(requestId, timeoutMs);
        try (Arena arena = Arena.ofConfined()) {
            submitRequest(parts, timeoutMs, flags,
                RoutedRequestSupport.replyCallback(),
                RoutedRequestSupport.userData(requestId));
            RequestReplySupport.startSocketRequestProgress(future,
                socket.handle(), "zlink-dealer-request-progress");
        } catch (RuntimeException ex) {
            RoutedRequestSupport.removePending(requestId);
            future.cancel(false);
            throw ex;
        }
        return future;
    }

    @Override
    public void close() {
        if (closeSocketOnClose) {
            socket.close();
        }
    }

    private void submitRequest(List<Message> payload,
                               long timeoutMs,
                               SendFlags flags,
                               MemorySegment handler,
                               MemorySegment userData) {
        int nativeFlags = flags == null ? 0 : flags.value();
        int timeout = RoutedRequestSupport.toTimeoutInt(timeoutMs);
        for (int i = 0; i < payload.size(); i++) {
            int partFlag = i + 1 < payload.size()
                ? Native.PART_MORE : Native.PART_FINAL;
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment nativeMsg = arena.allocate(
                    dev.kairoscode.zlink.internal.NativeLayouts.MSG_LAYOUT);
                payload.get(i).copyTo(nativeMsg);
                int rc = Native.dealerRequestPart(socket.handle(), nativeMsg,
                    nativeFlags, partFlag, i + 1 < payload.size() ? 0 : timeout,
                    i + 1 < payload.size() ? MemorySegment.NULL : handler,
                    i + 1 < payload.size() ? MemorySegment.NULL : userData);
                if (rc != SubmitResult.OK.value()) {
                    throw new SubmitException(SubmitResult.fromValue(rc));
                }
            }
        }
    }
}
