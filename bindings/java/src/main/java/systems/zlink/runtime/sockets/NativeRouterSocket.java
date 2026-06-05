/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.contracts.sockets.*;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.service.spot.ReplyOperation;
import systems.zlink.contracts.service.spot.RequestOperation;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.spot.SendOperation;
import systems.zlink.internal.ContractAccess;
import systems.zlink.runtime.messaging.MessageOperations;
import systems.zlink.runtime.nativeapi.InternalAccess;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;

final class NativeRouterSocket extends NativeSocketBase implements RouterSocket {
    private static final boolean DEBUG_REQREP =
      Boolean.getBoolean("zlink.reqrep.debug");
    private final RouterSocketOptions options = new RouterSocketOptions(this);
    private final Object routedRequests =
      InternalAccess.routerReceiveSupport(this, false);

    NativeRouterSocket(Context ctx) {
        super(ctx, SocketType.ROUTER);
    }

    public SendOperation send(RoutingId rid) {
        return MessageOperations.send(
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
        ContractAccess.receivedAdoptFrom(result, fresh);
        attachSendRouter(result);
        return true;
    }

    private void attachSendRouter(Received result) {
        if (result.getRoutingId().isPresent()) {
            attachSendSender(result);
        }
    }

    void attachSendSender(Received result) {
        RoutingId nodeRid = result.getRoutingId().orElse(null);
        if (nodeRid == null) return;
        RoutingId spotRid = result.spotRid().orElse(null);
        ContractAccess.receivedSetSendSender(result, (parts, flags) ->
            spotRid == null
                ? sendInternal(nodeRid, parts, flags)
                : sendToSpotInternal(nodeRid, spotRid, parts, flags));
    }
    public void setSendReadyHandler(SendReadyHandler handler) { super.setSendReadyHandler(handler); }

    public RequestOperation request(RoutingId rid) {
        return MessageOperations.request(
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

    public ReplyOperation reply(RoutingId rid, long requestSequence) {
        return MessageOperations.reply((parts, flags) ->
            InternalAccess.routerReply(this, rid, requestSequence, parts,
                flags));
    }

    public SendOperation sendToSpot(RoutingId destNodeRid, RoutingId destSpotRid) {
        return MessageOperations.send((parts, flags) ->
            sendToSpotInternal(destNodeRid, destSpotRid, parts, flags));
    }

    private boolean sendToSpotInternal(RoutingId destNodeRid, RoutingId destSpotRid,
                                       List<Message> parts, SendFlags flags) {
        return InternalAccess.routerSendToSpot(this, destNodeRid, destSpotRid,
            parts, flags);
    }

    public RequestOperation requestToSpot(RoutingId destNodeRid,
                                   RoutingId destSpotRid) {
        return MessageOperations.request(
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

    public ReplyOperation replyToSpot(RoutingId destNodeRid, RoutingId destSpotRid,
                               long requestSeq) {
        return MessageOperations.reply((parts, flags) ->
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
