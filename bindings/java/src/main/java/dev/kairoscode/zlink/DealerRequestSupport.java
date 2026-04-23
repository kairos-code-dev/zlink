/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import dev.kairoscode.zlink.internal.NativeRequestReplyBridge;
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
        return RequestReplySupport.startTimedRequestExecution(
            () -> submitRequest(parts, timeoutMs, flags), timeoutMs);
    }

    @Override
    public void close() {
        if (closeSocketOnClose) {
            socket.close();
        }
    }

    private static MemorySegment copyPayloadToNative(Arena arena,
                                                     List<Message> payload) {
        long msgSize = NativeLayouts.MSG_LAYOUT.byteSize();
        MemorySegment nativeParts = arena.allocate(msgSize * payload.size(),
            NativeLayouts.MSG_LAYOUT.byteAlignment());
        int built = 0;
        try {
            for (int i = 0; i < payload.size(); i++) {
                payload.get(i).copyTo(nativeParts.asSlice((long) i * msgSize,
                    msgSize));
                built++;
            }
            return nativeParts;
        } catch (RuntimeException ex) {
            for (int i = 0; i < built; i++) {
                try {
                    NativeMsg.msgClose(nativeParts.asSlice((long) i * msgSize,
                        msgSize));
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
            MemorySegment nativeParts = copyPayloadToNative(arena, payload);
            NativeRequestReplyBridge.ReplyResult reply =
                NativeRequestReplyBridge.dealerRequestSync(
                    socket.handle(), nativeParts, payload.size(),
                    flags == null ? 0 : flags.value(), toTimeoutInt(timeoutMs));
            if (reply.submitResult() != SubmitResult.OK.value()) {
                throw new SubmitException(SubmitResult.fromValue(reply.submitResult()));
            }
            if (reply.requestResult() != RequestResult.OK.value()) {
                throw new RequestException(
                    RequestResult.fromValue(reply.requestResult()),
                    reply.requestResult());
            }
            return new Received((RoutingId) null,
                Message.fromMsgVector(reply.replyParts(), reply.replyPartCount()),
                true);
        } catch (Throwable error) {
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
