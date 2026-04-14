/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.service.discovery.Discovery;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Duration;
import java.util.List;
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
    public void attachDiscovery(Discovery discovery) { super.attachDiscovery(discovery); }
    public void setRoutingId(RoutingId rid) { super.setRoutingId(rid); }
    public RoutingId routingId() { return super.routingId(); }
    public void send(Message part) { super.send(part); }
    public void send(Message part, SendFlags flags) { super.send(part, SendFlag.fromValue(flags.value())); }
    public void send(List<Message> parts) { super.send(parts); }
    public void send(List<Message> parts, SendFlags flags) { super.send(parts, SendFlag.fromValue(flags.value())); }
    public Received recv() { return super.recv(); }
    public Received recv(RecvFlags flags) { return super.recv(ReceiveFlag.fromValue(flags.value())); }
    public void onReceive(SocketMessageHandler handler) { super.onReceive(handler); }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
    public CompletableFuture<List<Message>> request(Message part) { return request(List.of(part)); }
    public CompletableFuture<List<Message>> request(Message part, Duration timeout) {
        return request(List.of(part), timeout);
    }
    public CompletableFuture<List<Message>> request(List<Message> parts) {
        return request(parts, Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }
    public CompletableFuture<List<Message>> request(List<Message> parts, Duration timeout) {
        return dealerRequests.request(parts, timeout).thenApply(reply -> {
            try (reply) {
                debug("dealer request thenApply parts=" + reply.parts().size());
                return RequestReplySupport.cloneReceived(reply).parts();
            }
        });
    }
    public void request(Message part, BiConsumer<RequestResult, List<Message>> callback) {
        request(List.of(part), callback);
    }
    public void request(List<Message> parts, BiConsumer<RequestResult, List<Message>> callback) {
        request(parts, callback, SendFlags.NONE,
            Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }
    public void request(Message part, BiConsumer<RequestResult, List<Message>> callback,
                        SendFlags flags) {
        request(List.of(part), callback, flags);
    }
    public void request(List<Message> parts, BiConsumer<RequestResult, List<Message>> callback,
                        SendFlags flags) {
        request(parts, callback, flags,
            Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }
    public void request(Message part, BiConsumer<RequestResult, List<Message>> callback,
                        SendFlags flags, Duration timeout) {
        request(List.of(part), callback, flags, timeout);
    }
    public void request(List<Message> parts, BiConsumer<RequestResult, List<Message>> callback,
                        SendFlags flags, Duration timeout) {
        dealerRequests.request(parts, (result, reply) -> {
            List<Message> payload = List.of();
            if (reply != null) {
                Received copy = RequestReplySupport.cloneReceived(reply);
                reply.close();
                payload = copy.parts();
            }
            callback.accept(result, payload);
        }, flags, timeout);
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
}
