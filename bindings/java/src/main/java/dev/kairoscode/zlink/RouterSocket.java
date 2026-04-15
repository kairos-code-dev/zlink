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

public final class RouterSocket extends Socket {
    private static final boolean DEBUG_REQREP =
      Boolean.getBoolean("zlink.reqrep.debug");
    private final RouterSocketOptions options = new RouterSocketOptions(this);
    private final RouterRequestSupport routedRequests =
      new RouterRequestSupport(this, false);
    private final RouterSpotSupport spotSupport = new RouterSpotSupport(this);

    public RouterSocket(Context ctx) {
        super(ctx, SocketType.ROUTER);
    }

    public void bind(String endpoint) { super.bind(endpoint); }
    public void connect(String endpoint) { super.connect(endpoint); }
    public void unbind(String endpoint) { super.unbind(endpoint); }
    public void disconnect(String endpoint) { super.disconnect(endpoint); }
    public void attachDiscovery(Discovery discovery) { super.attachDiscovery(discovery); }
    public void setRoutingId(RoutingId rid) { super.setRoutingId(rid); }
    public RoutingId routingId() { return super.routingId(); }
    public void send(RoutingId rid, Message part) { super.send(rid, part); }
    public void send(RoutingId rid, Message part, SendFlags flags) { super.send(rid, part, SendFlag.fromValue(flags.value())); }
    public void send(RoutingId rid, List<Message> parts) { super.send(rid, parts); }
    public void send(RoutingId rid, List<Message> parts, SendFlags flags) { super.send(rid, parts, SendFlag.fromValue(flags.value())); }
    public Received recv() { return routedRequests.recv(); }
    public Received recv(RecvFlags flags) { return routedRequests.recv(flags); }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
    public CompletableFuture<List<Message>> request(RoutingId rid, Message part) {
        return request(rid, List.of(part));
    }
    public CompletableFuture<List<Message>> request(RoutingId rid, Message part,
                                                    Duration timeout) {
        return request(rid, List.of(part), timeout);
    }
    public CompletableFuture<List<Message>> request(RoutingId rid, List<Message> parts) {
        return request(rid, parts, Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }
    public CompletableFuture<List<Message>> request(RoutingId rid, List<Message> parts,
                                                    Duration timeout) {
        return routedRequests.request(rid, parts, timeout).thenApply(reply -> {
            try (reply) {
                return RequestReplySupport.cloneReceived(reply).parts();
            }
        });
    }
    public void request(RoutingId rid, Message part,
                        BiConsumer<RequestResult, List<Message>> callback) {
        request(rid, List.of(part), callback);
    }
    public void request(RoutingId rid, Message part,
                        BiConsumer<RequestResult, List<Message>> callback,
                        SendFlags flags) {
        request(rid, List.of(part), callback, flags);
    }
    public void request(RoutingId rid, Message part,
                        BiConsumer<RequestResult, List<Message>> callback,
                        SendFlags flags, Duration timeout) {
        request(rid, List.of(part), callback, flags, timeout);
    }
    public void request(RoutingId rid, List<Message> parts,
                        BiConsumer<RequestResult, List<Message>> callback) {
        request(rid, parts, callback, SendFlags.NONE,
            Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }
    public void request(RoutingId rid, List<Message> parts,
                        BiConsumer<RequestResult, List<Message>> callback,
                        SendFlags flags) {
        request(rid, parts, callback, flags,
            Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }
    public void request(RoutingId rid, List<Message> parts,
                        BiConsumer<RequestResult, List<Message>> callback,
                        SendFlags flags, Duration timeout) {
        routedRequests.request(rid, parts, (result, reply) -> {
            List<Message> payload = List.of();
            if (reply != null) {
                Received copy = RequestReplySupport.cloneReceived(reply);
                reply.close();
                payload = copy.parts();
            }
            callback.accept(result, payload);
        }, flags, timeout);
    }
    public void reply(RoutingId rid, long requestSequence, Message part) {
        reply(rid, requestSequence, List.of(part));
    }
    public void reply(RoutingId rid, long requestSequence, Message part,
                      SendFlags flags) {
        reply(rid, requestSequence, List.of(part), flags);
    }
    public void reply(RoutingId rid, long requestSequence, List<Message> parts) {
        routedRequests.reply(rid, requestSequence, parts);
    }
    public void reply(RoutingId rid, long requestSequence, List<Message> parts,
                      SendFlags flags) {
        routedRequests.reply(rid, requestSequence, parts, flags);
    }
    public void sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                           Message part) {
        sendToSpot(destNodeRid, destSpotRid, List.of(part), SendFlags.NONE);
    }
    public void sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                           Message part, SendFlags flags) {
        sendToSpot(destNodeRid, destSpotRid, List.of(part), flags);
    }
    public void sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                           List<Message> parts) {
        sendToSpot(destNodeRid, destSpotRid, parts, SendFlags.NONE);
    }
    public void sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                           List<Message> parts, SendFlags flags) {
        spotSupport.sendToSpot(destNodeRid, destSpotRid, parts, flags);
    }
    public CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                                          RoutingId destSpotRid,
                                                          Message part) {
        return requestToSpot(destNodeRid, destSpotRid, List.of(part));
    }
    public CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                                          RoutingId destSpotRid,
                                                          Message part,
                                                          Duration timeout) {
        return requestToSpot(destNodeRid, destSpotRid, List.of(part), timeout);
    }
    public CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                                          RoutingId destSpotRid,
                                                          List<Message> parts) {
        return requestToSpot(destNodeRid, destSpotRid, parts,
          Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }
    public CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                                          RoutingId destSpotRid,
                                                          List<Message> parts,
                                                          Duration timeout) {
        return spotSupport.requestToSpot(destNodeRid, destSpotRid, parts,
          timeout);
    }
    public void requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                              Message part,
                              BiConsumer<RequestResult, List<Message>> callback) {
        requestToSpot(destNodeRid, destSpotRid, List.of(part), callback);
    }
    public void requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                              Message part,
                              BiConsumer<RequestResult, List<Message>> callback,
                              SendFlags flags) {
        requestToSpot(destNodeRid, destSpotRid, List.of(part), callback, flags);
    }
    public void requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                              Message part,
                              BiConsumer<RequestResult, List<Message>> callback,
                              SendFlags flags, Duration timeout) {
        requestToSpot(destNodeRid, destSpotRid, List.of(part), callback, flags,
          timeout);
    }
    public void requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                              List<Message> parts,
                              BiConsumer<RequestResult, List<Message>> callback) {
        requestToSpot(destNodeRid, destSpotRid, parts, callback, SendFlags.NONE,
          Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }
    public void requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                              List<Message> parts,
                              BiConsumer<RequestResult, List<Message>> callback,
                              SendFlags flags) {
        requestToSpot(destNodeRid, destSpotRid, parts, callback, flags,
          Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }
    public void requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                              List<Message> parts,
                              BiConsumer<RequestResult, List<Message>> callback,
                              SendFlags flags, Duration timeout) {
        spotSupport.requestToSpot(destNodeRid, destSpotRid, parts, callback,
          flags, timeout);
    }
    public void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                            long requestSeq, Message message) {
        replyToSpot(destNodeRid, destSpotRid, requestSeq, List.of(message),
          SendFlags.NONE);
    }
    public void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                            long requestSeq, Message message, SendFlags flags) {
        replyToSpot(destNodeRid, destSpotRid, requestSeq, List.of(message),
          flags);
    }
    public void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                            long requestSeq, List<Message> parts) {
        replyToSpot(destNodeRid, destSpotRid, requestSeq, parts, SendFlags.NONE);
    }
    public void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                            long requestSeq, List<Message> parts,
                            SendFlags flags) {
        spotSupport.replyToSpot(destNodeRid, destSpotRid, requestSeq, parts,
          flags);
    }
    @Override
    public void close() {
        debug("router close begin");
        routedRequests.beginClose();
        try {
            super.close();
        } finally {
            routedRequests.finishClose();
        }
        debug("router close end");
    }
    @Override public RouterSocketOptions options() { return options; }

    private static void debug(String message) {
        if (DEBUG_REQREP) {
            try {
                Files.writeString(Path.of("/tmp/zlink-reqrep.log"),
                    "[router-socket] " + message + System.lineSeparator(),
                    StandardOpenOption.CREATE, StandardOpenOption.APPEND);
            } catch (Exception ignored) {
            }
        }
    }
}
