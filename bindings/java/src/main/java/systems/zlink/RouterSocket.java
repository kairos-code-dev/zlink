/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import systems.zlink.service.discovery.Discovery;
import systems.zlink.service.spot.ReplyOp;
import systems.zlink.service.spot.RequestOp;
import systems.zlink.service.spot.SendOp;
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
    public SendOp send(RoutingId rid) {
        return SocketOperations.send((parts, flags) ->
            sendInternal(rid, parts, flags));
    }

    boolean sendInternal(RoutingId rid, List<Message> parts, SendFlags flags) {
        return super.send(rid, parts, SendFlag.fromValue(flags.value()));
    }
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
            boolean ok = routedRequests.recvInto(result, flags);
            if (ok) attachSendRouter(result);
            return ok;
        }
        Received fresh = routedRequests.recv(flags);
        if (fresh == null) return false;
        result.adoptFrom(fresh);
        attachSendRouter(result);
        return true;
    }

    private void attachSendRouter(Received result) {
        if (result.hasSendSender()) return;
        if (result.routingIdOrNull() != null) {
            result.setSendRouter(this);
        }
    }
    public void onSendReady(SendReadyHandler handler) { super.onSendReady(handler); }

    public RequestOp request(RoutingId rid) {
        return SocketOperations.request(
            (parts, flags, timeout) -> requestAsync(rid, parts, flags, timeout),
            (parts, callback, flags, timeout) ->
                requestCallback(rid, parts, callback, flags, timeout));
    }

    private CompletableFuture<List<Message>> requestAsync(RoutingId rid,
                                                          List<Message> parts,
                                                          SendFlags flags,
                                                          Duration timeout) {
        return routedRequests.request(rid, parts, flags, timeout).thenApply(reply ->
            RequestReplySupport.takeReceivedParts(reply));
    }

    private boolean requestCallback(RoutingId rid,
                                    List<Message> parts,
                                    RequestCallback callback,
                                    SendFlags flags,
                                    Duration timeout) {
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

    public ReplyOp reply(RoutingId rid, long requestSequence) {
        return SocketOperations.reply((parts, flags) ->
            routedRequests.reply(rid, requestSequence, parts, flags));
    }

    public SendOp sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid) {
        return SocketOperations.send((parts, flags) ->
            sendToSpotInternal(destNodeRid, destSpotRid, parts, flags));
    }

    boolean sendToSpotInternal(RoutingId destNodeRid, RoutingId destSpotRid,
                               List<Message> parts, SendFlags flags) {
        return spotSupport.sendToSpot(destNodeRid, destSpotRid, parts, flags);
    }

    public RequestOp requestToSpot(RoutingId destNodeRid,
                                   RoutingId destSpotRid) {
        return SocketOperations.request(
            (parts, flags, timeout) ->
                requestToSpotAsync(destNodeRid, destSpotRid, parts, flags,
                  timeout),
            (parts, callback, flags, timeout) ->
                requestToSpotCallback(destNodeRid, destSpotRid, parts,
                  callback, flags, timeout));
    }

    private CompletableFuture<List<Message>> requestToSpotAsync(
            RoutingId destNodeRid,
            RoutingId destSpotRid,
            List<Message> parts,
            SendFlags flags,
            Duration timeout) {
        return spotSupport.requestToSpot(destNodeRid, destSpotRid, parts,
          timeout, flags);
    }

    private boolean requestToSpotCallback(RoutingId destNodeRid,
                                          RoutingId destSpotRid,
                                          List<Message> parts,
                                          RequestCallback callback,
                                          SendFlags flags,
                                          Duration timeout) {
        return spotSupport.requestToSpot(destNodeRid, destSpotRid, parts,
          callback::onComplete, flags, timeout);
    }

    public ReplyOp replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                               long requestSeq) {
        return SocketOperations.reply((parts, flags) ->
            spotSupport.replyToSpot(destNodeRid, destSpotRid, requestSeq,
              parts, flags));
    }

    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, List<Message> parts, SendFlags flags) {
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
