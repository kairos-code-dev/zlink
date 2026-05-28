/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.contracts.sockets.*;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.service.discovery.Discovery;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.service.spot.ReplyOp;
import systems.zlink.contracts.service.spot.RequestOp;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.SendOp;
import systems.zlink.contracts.internal.ContractAccess;
import systems.zlink.runtime.nativeapi.InternalAccess;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;

public final class NativeRouterSocket extends NativeSocketBase implements RouterSocket {
    private static final boolean DEBUG_REQREP =
      Boolean.getBoolean("zlink.reqrep.debug");
    private final RouterSocketOptions options = new RouterSocketOptions(this);
    private final Object routedRequests =
      InternalAccess.routerReceiveSupport(this, false);

    NativeRouterSocket(Context ctx) {
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
        return SocketOperations.send(
            (part, flags) -> sendInternal(rid, part, flags),
            (parts, flags) -> sendInternal(rid, parts, flags));
    }

    private boolean sendInternal(RoutingId rid, Message part, SendFlags flags) {
        return super.send(rid, part, SendFlag.fromValue(flags.value()));
    }
    private boolean sendInternal(RoutingId rid, List<Message> parts, SendFlags flags) {
        return super.send(rid, parts, SendFlag.fromValue(flags.value()));
    }
    /** Canonical caller-provided storage recv. See doc/spec/bindings/README.md. */
    public boolean recv(Received result, RecvFlags flags) {
        java.util.Objects.requireNonNull(result, "result");
        java.util.Objects.requireNonNull(flags, "flags");
        if (flags == RecvFlags.DONT_WAIT) {
            boolean ok = InternalAccess.routerRecvInto(routedRequests, result,
                flags);
            if (ok) attachSendRouter(result);
            return ok;
        }
        Received fresh = InternalAccess.routerRecv(routedRequests, flags);
        if (fresh == null) return false;
        result.adoptFrom(fresh);
        attachSendRouter(result);
        return true;
    }

    private void attachSendRouter(Received result) {
        if (result.routingId().isPresent()) {
            attachSendSender(result);
        }
    }

    void attachSendSender(Received result) {
        RoutingId nodeRid = result.routingId().orElse(null);
        if (nodeRid == null) return;
        RoutingId spotRid = result.spotRid().orElse(null);
        ContractAccess.receivedSetSendSender(result, (parts, flags) ->
            spotRid == null
                ? sendInternal(nodeRid, parts, flags)
                : sendToSpotInternal(nodeRid, spotRid, parts, flags));
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
        return InternalAccess.routerRequestAsync(this, rid, parts, flags,
            timeout);
    }

    private boolean requestCallback(RoutingId rid,
                                    List<Message> parts,
                                    RequestCallback callback,
                                    SendFlags flags,
                                    Duration timeout) {
        return InternalAccess.routerRequestCallback(this, rid, parts, callback,
            flags, timeout);
    }

    public ReplyOp reply(RoutingId rid, long requestSequence) {
        return SocketOperations.reply((parts, flags) ->
            InternalAccess.routerReply(this, rid, requestSequence, parts,
                flags));
    }

    public SendOp sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid) {
        return SocketOperations.send((parts, flags) ->
            sendToSpotInternal(destNodeRid, destSpotRid, parts, flags));
    }

    private boolean sendToSpotInternal(RoutingId destNodeRid, RoutingId destSpotRid,
                                       List<Message> parts, SendFlags flags) {
        return InternalAccess.routerSendToSpot(this, destNodeRid, destSpotRid,
            parts, flags);
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
        return InternalAccess.routerRequestToSpotAsync(this, destNodeRid,
          destSpotRid, parts, timeout, flags);
    }

    private boolean requestToSpotCallback(RoutingId destNodeRid,
                                          RoutingId destSpotRid,
                                          List<Message> parts,
                                          RequestCallback callback,
                                          SendFlags flags,
                                          Duration timeout) {
        return InternalAccess.routerRequestToSpotCallback(this, destNodeRid,
          destSpotRid, parts, callback::onComplete, flags, timeout);
    }

    public ReplyOp replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                               long requestSeq) {
        return SocketOperations.reply((parts, flags) ->
            InternalAccess.routerReplyToSpot(this, destNodeRid, destSpotRid,
              requestSeq, parts, flags));
    }

    void replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                     long requestSeq, List<Message> parts, SendFlags flags) {
        InternalAccess.routerReplyToSpot(this, destNodeRid, destSpotRid,
          requestSeq, parts, flags);
    }
    @Override
    public void close() {
        debug("router close begin");
        InternalAccess.routerReceiveBeginClose(routedRequests);
        try {
            super.close();
        } finally {
            InternalAccess.routerReceiveFinishClose(routedRequests);
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
