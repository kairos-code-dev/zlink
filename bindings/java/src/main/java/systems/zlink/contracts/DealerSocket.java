/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.service.spot.RequestOp;
import systems.zlink.contracts.service.spot.SendOp;
import systems.zlink.runtime.nativebridge.Native;
import systems.zlink.runtime.nativebridge.NativeLayouts;
import systems.zlink.runtime.nativebridge.RequestReplySupport;
import systems.zlink.runtime.nativebridge.RoutedRequestSupport;
import systems.zlink.runtime.nativebridge.SocketOperations;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.function.BiConsumer;

public final class DealerSocket extends Socket {
    private static final boolean DEBUG_REQREP =
      Boolean.getBoolean("zlink.reqrep.debug");
    private final DealerSocketOptions options = new DealerSocketOptions(this);
    private final DealerRequestSupport dealerRequests =
      new DealerRequestSupport(this, false);

    public DealerSocket(Context ctx) {
        super(ctx, SocketType.DEALER);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void connect(String endpoint) { super.connect(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public void disconnect(String endpoint) { super.disconnect(endpoint); }
    public void disconnectRid(RoutingId routingId) { super.disconnectRid(routingId); }
    public void attachDiscovery(Discovery discovery) { super.attachDiscovery(discovery); }
    public void setChannelName(String channelName) { super.setChannelName(channelName); }
    public String getChannelName() { return super.getChannelName(); }
    public void setRoutingId(RoutingId rid) { super.setRoutingId(rid); }
    public RoutingId routingId() { return super.routingId(); }
    public SendOp send() {
        return SocketOperations.send(
            (part, flags) -> super.send(part, SendFlag.fromValue(flags.value())),
            (parts, flags) ->
            super.send(parts, SendFlag.fromValue(flags.value())));
    }
    SendResult sendNoWaitResult(Message part) { return super.sendNoWaitResult(part); }
    SendResult sendNoWaitResult(List<Message> parts) { return super.sendNoWaitResult(parts); }
    /**
     * Canonical caller-provided storage recv. Pass a long-lived
     * {@link Received} and the binding refills its internal state in place
     * each successful call.
     *
     * @return {@code true} on success, {@code false} when
     * {@link RecvFlags#DONT_WAIT} finds no data.
     */
    public boolean recv(Received result, RecvFlags flags) {
        java.util.Objects.requireNonNull(result, "result");
        java.util.Objects.requireNonNull(flags, "flags");
        return super.recvInto(result, ReceiveFlag.fromValue(flags.value()));
    }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
    public RequestOp request() {
        return SocketOperations.request(this::requestAsync, this::requestCallback);
    }

    private CompletableFuture<List<Message>> requestAsync(List<Message> parts,
                                                          SendFlags flags,
                                                          Duration timeout) {
        return dealerRequests.request(parts, timeout, flags).thenApply(reply ->
            RequestReplySupport.takeReceivedParts(reply));
    }

    private boolean requestCallback(List<Message> parts,
                                    RequestCallback callback,
                                    SendFlags flags,
                                    Duration timeout) {
        try {
            dealerRequests.request(parts, (result, reply) -> {
                List<Message> payload = List.of();
                if (reply != null) {
                    payload = RequestReplySupport.takeReceivedParts(reply);
                }
                callback.onComplete(result, payload);
            }, flags, timeout);
            return true;
        } catch (SubmitException ex) {
            if (flags == SendFlags.DONT_WAIT
                && ex.getResult() == SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        }
    }
    @Override
    public void close() {
        debug("dealer close begin");
        try {
            super.close();
        } finally {
            debug("dealer close end");
        }
    }
    @Override public DealerSocketOptions options() { return options; }

    private static void debug(String message) {
        if (DEBUG_REQREP) {
            try {
                Files.writeString(Path.of("/tmp/zlink-reqrep.log"),
                    "[dealer-socket] " + message + System.lineSeparator(),
                    StandardOpenOption.CREATE, StandardOpenOption.APPEND);
            } catch (Exception ignored) {
            }
        }
    }

    private static final class DealerRequestSupport implements AutoCloseable {
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
                        systems.zlink.runtime.nativebridge.NativeLayouts.MSG_LAYOUT);
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
}
