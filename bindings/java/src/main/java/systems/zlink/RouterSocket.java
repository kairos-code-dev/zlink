/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import systems.zlink.service.discovery.Discovery;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;

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
    public void disconnectRid(RoutingId routingId) { super.disconnectRid(routingId); }
    public void attachDiscovery(Discovery discovery) { super.attachDiscovery(discovery); }
    public void setRoutingId(RoutingId rid) { super.setRoutingId(rid); }
    public RoutingId routingId() { return super.routingId(); }
    public boolean send(RoutingId rid, Message part) { return super.send(rid, part); }
    public boolean send(RoutingId rid, Message part, SendFlags flags) { return super.send(rid, part, SendFlag.fromValue(flags.value())); }
    public boolean send(RoutingId rid, List<Message> parts) { return super.send(rid, parts); }
    public boolean send(RoutingId rid, List<Message> parts, SendFlags flags) { return super.send(rid, parts, SendFlag.fromValue(flags.value())); }
    SendResult sendNoWaitResult(RoutingId rid, Message part) {
        return super.sendNoWaitResult(rid, part);
    }
    SendResult sendNoWaitResult(RoutingId rid, List<Message> parts) {
        return super.sendNoWaitResult(rid, parts);
    }
    /** Canonical caller-provided storage recv. See doc/spec/bindings/README.md. */
    public boolean recv(Received result, RecvFlags flags) {
        java.util.Objects.requireNonNull(result, "result");
        java.util.Objects.requireNonNull(flags, "flags");
        if (flags == RecvFlags.DONT_WAIT) {
            return routedRequests.recvInto(result, flags);
        }
        Received fresh = routedRequests.recv(flags);
        if (fresh == null) return false;
        result.adoptFrom(fresh);
        return true;
    }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }
    public CompletableFuture<List<Message>> request(RoutingId rid, Message part) {
        return request(rid, List.of(part));
    }
    private CompletableFuture<List<Message>> request(RoutingId rid, Message part,
                                                     SendFlags flags) {
        return request(rid, List.of(part), flags);
    }
    public CompletableFuture<List<Message>> request(RoutingId rid, Message part,
                                                    Duration timeout) {
        return request(rid, List.of(part), timeout);
    }
    private CompletableFuture<List<Message>> request(RoutingId rid, Message part,
                                                     SendFlags flags,
                                                     Duration timeout) {
        return request(rid, List.of(part), flags, timeout);
    }
    public CompletableFuture<List<Message>> request(RoutingId rid, List<Message> parts) {
        return request(rid, parts, SendFlags.NONE);
    }
    private CompletableFuture<List<Message>> request(RoutingId rid, List<Message> parts,
                                                     SendFlags flags) {
        return request(rid, parts, flags,
            Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }
    public CompletableFuture<List<Message>> request(RoutingId rid, List<Message> parts,
                                                    Duration timeout) {
        return request(rid, parts, SendFlags.NONE, timeout);
    }
    private CompletableFuture<List<Message>> request(RoutingId rid, List<Message> parts,
                                                     SendFlags flags,
                                                     Duration timeout) {
        return routedRequests.request(rid, parts, flags, timeout).thenApply(reply ->
            RequestReplySupport.takeReceivedParts(reply));
    }
    public boolean request(RoutingId rid, Message part,
                        RequestCallback callback) {
        return request(rid, List.of(part), callback);
    }
    public boolean request(RoutingId rid, Message part,
                        RequestCallback callback,
                        Duration timeout) {
        return request(rid, List.of(part), callback, SendFlags.NONE, timeout);
    }
    public boolean request(RoutingId rid, List<Message> parts,
                        RequestCallback callback,
                        Duration timeout) {
        return request(rid, parts, callback, SendFlags.NONE, timeout);
    }
    public boolean request(RoutingId rid, Message part,
                        RequestCallback callback,
                        SendFlags flags) {
        return request(rid, List.of(part), callback, flags);
    }
    public boolean request(RoutingId rid, Message part,
                        RequestCallback callback,
                        SendFlags flags, Duration timeout) {
        return request(rid, List.of(part), callback, flags, timeout);
    }
    public boolean request(RoutingId rid, List<Message> parts,
                        RequestCallback callback) {
        return request(rid, parts, callback, SendFlags.NONE,
            Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }
    public boolean request(RoutingId rid, List<Message> parts,
                        RequestCallback callback,
                        SendFlags flags) {
        return request(rid, parts, callback, flags,
            Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }
    public boolean request(RoutingId rid, List<Message> parts,
                        RequestCallback callback,
                        SendFlags flags, Duration timeout) {
        try {
            routedRequests.request(rid, parts, (result, reply) -> {
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
    public boolean sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                           Message part) {
        return sendToSpot(destNodeRid, destSpotRid, List.of(part), SendFlags.NONE);
    }
    public boolean sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                           Message part, SendFlags flags) {
        return sendToSpot(destNodeRid, destSpotRid, List.of(part), flags);
    }
    public boolean sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                           List<Message> parts) {
        return sendToSpot(destNodeRid, destSpotRid, parts, SendFlags.NONE);
    }
    public boolean sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                           List<Message> parts, SendFlags flags) {
        return spotSupport.sendToSpot(destNodeRid, destSpotRid, parts, flags);
    }
    public CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                                          RoutingId destSpotRid,
                                                          Message part) {
        return requestToSpot(destNodeRid, destSpotRid, List.of(part));
    }
    private CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                                           RoutingId destSpotRid,
                                                           Message part,
                                                           SendFlags flags) {
        return requestToSpot(destNodeRid, destSpotRid, List.of(part), flags);
    }
    public CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                                          RoutingId destSpotRid,
                                                          Message part,
                                                          Duration timeout) {
        return requestToSpot(destNodeRid, destSpotRid, List.of(part), timeout);
    }
    private CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                                           RoutingId destSpotRid,
                                                           Message part,
                                                           SendFlags flags,
                                                           Duration timeout) {
        return requestToSpot(destNodeRid, destSpotRid, List.of(part), flags,
          timeout);
    }
    public CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                                          RoutingId destSpotRid,
                                                          List<Message> parts) {
        return requestToSpot(destNodeRid, destSpotRid, parts, SendFlags.NONE);
    }
    private CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                                           RoutingId destSpotRid,
                                                           List<Message> parts,
                                                           SendFlags flags) {
        return requestToSpot(destNodeRid, destSpotRid, parts, flags,
          Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }
    public CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                                          RoutingId destSpotRid,
                                                          List<Message> parts,
                                                          Duration timeout) {
        return requestToSpot(destNodeRid, destSpotRid, parts, SendFlags.NONE,
          timeout);
    }
    private CompletableFuture<List<Message>> requestToSpot(RoutingId destNodeRid,
                                                           RoutingId destSpotRid,
                                                           List<Message> parts,
                                                           SendFlags flags,
                                                           Duration timeout) {
        return spotSupport.requestToSpot(destNodeRid, destSpotRid, parts,
          timeout, flags);
    }
    public boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                              Message part,
                              RequestCallback callback) {
        return requestToSpot(destNodeRid, destSpotRid, List.of(part), callback);
    }
    public boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                              Message part,
                              RequestCallback callback,
                              Duration timeout) {
        return requestToSpot(destNodeRid, destSpotRid, List.of(part), callback,
          SendFlags.NONE, timeout);
    }
    public boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                              List<Message> parts,
                              RequestCallback callback,
                              Duration timeout) {
        return requestToSpot(destNodeRid, destSpotRid, parts, callback,
          SendFlags.NONE, timeout);
    }
    public boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                              Message part,
                              RequestCallback callback,
                              SendFlags flags) {
        return requestToSpot(destNodeRid, destSpotRid, List.of(part), callback, flags);
    }
    public boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                              Message part,
                              RequestCallback callback,
                              SendFlags flags, Duration timeout) {
        return requestToSpot(destNodeRid, destSpotRid, List.of(part), callback, flags,
          timeout);
    }
    public boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                              List<Message> parts,
                              RequestCallback callback) {
        return requestToSpot(destNodeRid, destSpotRid, parts, callback, SendFlags.NONE,
          Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }
    public boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                              List<Message> parts,
                              RequestCallback callback,
                              SendFlags flags) {
        return requestToSpot(destNodeRid, destSpotRid, parts, callback, flags,
          Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }
    public boolean requestToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                              List<Message> parts,
                              RequestCallback callback,
                              SendFlags flags, Duration timeout) {
        return spotSupport.requestToSpot(destNodeRid, destSpotRid, parts,
          callback::onComplete, flags, timeout);
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
