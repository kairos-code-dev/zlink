/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.sockets;

import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.service.spot.ActorRef;
import systems.zlink.contracts.service.spot.ReplyHandler;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.sockets.DealerSocket;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RequestCallback;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.sockets.RouterSocket;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.StreamSocket;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.RequestReplySupport;
import systems.zlink.runtime.nativeapi.RoutedRequestSupport;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.CompletableFuture;
import java.util.function.BiConsumer;

final class NativeDealerRequestSupport {
    static {
        InternalAccess.register(new InternalAccess.RuntimeSocketAccess() {
            @Override
            public CompletableFuture<List<Message>> dealerRequestAsync(
                    DealerSocket socket, List<Message> parts, SendFlags flags,
                    Duration timeout) {
                return NativeDealerRequestSupport.requestStage(socket, parts,
                    flags, timeout);
            }

            @Override
            public boolean dealerRequestCallback(
                    DealerSocket socket, List<Message> parts,
                    RequestCallback callback, SendFlags flags,
                    Duration timeout) {
                return NativeDealerRequestSupport.requestCallback(socket, parts,
                    callback, flags, timeout);
            }

            @Override
            public Object routerReceiveSupport(RouterSocket socket,
                                               boolean closeSocketOnClose) {
                return new NativeRouterReceiveSupport(socket,
                    closeSocketOnClose);
            }

            @Override
            public Received routerRecv(Object support, RecvFlags flags) {
                return ((NativeRouterReceiveSupport) support).recv(flags);
            }

            @Override
            public boolean routerRecvInto(Object support, Received target,
                                          RecvFlags flags) {
                return ((NativeRouterReceiveSupport) support).recvInto(target,
                    flags);
            }

            @Override
            public void routerOnReceive(Object support,
                                        SocketMessageHandler handler) {
                ((NativeRouterReceiveSupport) support).onReceive(handler);
            }

            @Override
            public void routerReceiveBeginClose(Object support) {
                ((NativeRouterReceiveSupport) support).beginClose();
            }

            @Override
            public void routerReceiveFinishClose(Object support) {
                ((NativeRouterReceiveSupport) support).finishClose();
            }

            @Override
            public CompletableFuture<List<Message>> routerRequestAsync(
                    RouterSocket socket, RoutingId routingId,
                    List<Message> parts, SendFlags flags, Duration timeout) {
                return NativeRouterRequestSupport.requestStage(socket,
                    routingId, parts, flags, timeout);
            }

            @Override
            public boolean routerRequestCallback(
                    RouterSocket socket, RoutingId routingId,
                    List<Message> parts, RequestCallback callback,
                    SendFlags flags, Duration timeout) {
                return NativeRouterRequestSupport.requestCallback(socket,
                    routingId, parts, callback, flags, timeout);
            }

            @Override
            public void routerReply(RouterSocket socket, RoutingId routingId,
                                    long requestSequence, List<Message> parts,
                                    SendFlags flags) {
                NativeRouterRequestSupport.reply(socket, routingId,
                    requestSequence, parts, flags);
            }

            @Override
            public boolean routerSendToSpot(
                    RouterSocket socket, RoutingId destNodeRid,
                    RoutingId destSpotRid, List<Message> parts,
                    SendFlags flags) {
                return NativeRouterSpotSupport.sendToSpot(socket, destNodeRid,
                    destSpotRid, parts, flags);
            }

            @Override
            public CompletableFuture<List<Message>> routerRequestToSpotAsync(
                    RouterSocket socket, RoutingId destNodeRid,
                    RoutingId destSpotRid, List<Message> parts,
                    Duration timeout, SendFlags flags) {
                return NativeRouterSpotSupport.requestToSpot(socket,
                    destNodeRid, destSpotRid, parts, timeout, flags);
            }

            @Override
            public boolean routerRequestToSpotCallback(
                    RouterSocket socket, RoutingId destNodeRid,
                    RoutingId destSpotRid, List<Message> parts,
                    BiConsumer<RequestResult, List<Message>> callback,
                    SendFlags flags, Duration timeout) {
                return NativeRouterSpotSupport.requestToSpot(socket,
                    destNodeRid, destSpotRid, parts, callback, flags, timeout);
            }

            @Override
            public void routerReplyToSpot(
                    RouterSocket socket, RoutingId destNodeRid,
                    RoutingId destSpotRid, long requestSeq,
                    List<Message> parts, SendFlags flags) {
                NativeRouterSpotSupport.replyToSpot(socket, destNodeRid,
                    destSpotRid, requestSeq, parts, flags);
            }

            @Override
            public List<ActorRef> streamBoundActors(StreamSocket socket,
                                                    RoutingId sessionRid) {
                return NativeStreamActorSupport.boundActors(socket, sessionRid);
            }

            @Override
            public boolean streamSubmitBind(
                    StreamSocket socket, RoutingId sessionRid, ActorRef actor,
                    Duration timeout, ReplyHandler callback) {
                return NativeStreamActorSupport.submitBind(socket, sessionRid,
                    actor, timeout, callback);
            }

            @Override
            public boolean streamSubmitUnbind(
                    StreamSocket socket, RoutingId sessionRid, String actorId,
                    Duration timeout, ReplyHandler callback) {
                return NativeStreamActorSupport.submitUnbind(socket,
                    sessionRid, actorId, timeout, callback);
            }

            @Override
            public boolean streamSendBoundActorReceiveds(
                    StreamSocket socket, RoutingId sessionRid, String actorId,
                    List<Message> parts, SendFlags flags) {
                return NativeStreamActorSupport.sendBoundActorReceiveds(socket,
                    sessionRid, actorId, parts, flags);
            }
        });
    }

    private NativeDealerRequestSupport() {
    }

    public static CompletableFuture<List<Message>> requestStage(
            DealerSocket socket,
            List<Message> parts,
            SendFlags flags,
            Duration timeout) {
        return request(socket, parts, timeout, flags)
            .thenApply(RequestReplySupport::takeReceivedParts);
    }

    public static boolean requestCallback(DealerSocket socket,
                                          List<Message> parts,
                                          RequestCallback callback,
                                          SendFlags flags,
                                          Duration timeout) {
        Objects.requireNonNull(callback, "callback");
        try {
            request(socket, parts, timeout, flags).whenComplete((reply, error) -> {
                List<Message> payload = List.of();
                if (reply != null) {
                    payload = RequestReplySupport.takeReceivedParts(reply);
                }
                callback.onComplete(error == null ? RequestResult.OK
                    : RequestReplySupport.requestResult(error), payload);
            });
            return true;
        } catch (ZlinkSubmitException ex) {
            if (flags == SendFlags.DONT_WAIT
                && ex.getResult() == SubmitResult.BACKPRESSURED) {
                return false;
            }
            throw ex;
        }
    }

    private static CompletableFuture<Received> request(DealerSocket socket,
                                                       List<Message> parts,
                                                       Duration timeout,
                                                       SendFlags flags) {
        Objects.requireNonNull(socket, "socket");
        Objects.requireNonNull(parts, "parts");
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        long requestId = RoutedRequestSupport.nextRequestId();
        CompletableFuture<Received> future =
            RoutedRequestSupport.registerPending(requestId, timeoutMs);
        try {
            submitRequest(socket, parts, timeoutMs, flags,
                RoutedRequestSupport.replyCallback(),
                RoutedRequestSupport.userData(requestId));
            RequestReplySupport.startSocketRequestProgress(future,
                InternalAccess.socketHandle(socket),
                "zlink-dealer-request-progress");
        } catch (RuntimeException ex) {
            RoutedRequestSupport.removePending(requestId);
            future.cancel(false);
            throw ex;
        }
        return future;
    }

    private static void submitRequest(DealerSocket socket,
                                      List<Message> payload,
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
                MemorySegment nativeMsg = arena.allocate(NativeLayouts.MESSAGE_LAYOUT);
                InternalAccess.messageCopyTo(payload.get(i), nativeMsg);
                int rc = Native.dealerRequestPart(
                    InternalAccess.socketHandle(socket), nativeMsg,
                    nativeFlags, partFlag,
                    timeout,
                    handler,
                    userData);
                if (rc != SubmitResult.OK.value()) {
                    throw new ZlinkSubmitException(SubmitResult.fromValue(rc));
                }
            }
        }
    }
}
