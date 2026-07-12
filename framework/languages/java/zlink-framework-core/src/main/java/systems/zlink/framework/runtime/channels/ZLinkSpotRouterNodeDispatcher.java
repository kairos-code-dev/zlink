package systems.zlink.framework.runtime.channels;

import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.BiConsumer;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestResult;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;

final class ZLinkSpotRouterNodeDispatcher {
    private ZLinkSpotRouterNodeDispatcher() {
    }

    static CompletionStage<Void> send(
        String routerChannelId,
        ZLinkInternalSpotNode node,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<Message> spotParts) {
        CompletableFuture<Void> result = new CompletableFuture<>();
        List<Message> requestParts = ZLinkChannelRuntime.copyMessages(spotParts);
        try {
            boolean submitted = node.entrySpot().sendToSpot(
                targetNodeRid,
                targetSpotRid,
                requestParts,
                SendFlags.NONE);
            ZLinkChannelRuntime.trace("spot-route node-send-submit router=" + routerChannelId
                + " targetNode=" + targetNodeRid
                + " targetSpot=" + targetSpotRid
                + " submitted=" + submitted);
            if (submitted) {
                result.complete(null);
            } else {
                result.completeExceptionally(new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.REQUEST_FAILED,
                    "Spot node router '" + routerChannelId + "' is not ready for SPOT send."));
            }
        } catch (RuntimeException ex) {
            result.completeExceptionally(ex);
        } finally {
            requestParts.forEach(Message::close);
        }
        return result;
    }

    static CompletionStage<List<Message>> request(
        String routerChannelId,
        ZLinkInternalSpotNode node,
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        List<Message> spotParts,
        Duration timeout,
        BiConsumer<CompletableFuture<List<Message>>, Duration> trackPendingRequest) {
        CompletableFuture<List<Message>> result = new CompletableFuture<>();
        trackPendingRequest.accept(result, timeout);
        List<Message> requestParts = ZLinkChannelRuntime.copyMessages(spotParts);
        long requestStartedNanos = System.nanoTime();
        try {
            boolean submitted = node.entrySpot().requestToSpot(
                targetNodeRid,
                targetSpotRid,
                requestParts,
                reply -> {
                    try {
                        ZLinkChannelRuntime.trace("spot-route node-reply router=" + routerChannelId
                            + " targetNode=" + targetNodeRid
                            + " targetSpot=" + targetSpotRid
                            + " elapsedMs=" + ZLinkChannelRuntime.elapsedMillis(requestStartedNanos)
                            + " result=" + reply.result()
                            + " origin=spot-node-callback"
                            + " sourceRid=" + reply.routingId().map(Object::toString).orElse(null)
                            + " sourceSpot=" + reply.spotRid().map(Object::toString).orElse(null)
                            + " requestSeq=" + reply.requestSeq().map(Object::toString).orElse(null)
                            + " parts=" + ZLinkChannelRuntime.describeTraceParts(reply.parts()));
                        if (reply.result() != ZLinkBackendRequestResult.OK) {
                            result.completeExceptionally(new ZLinkFrameworkException(
                                ZLinkFrameworkErrorKind.REQUEST_FAILED,
                                "SPOT route request failed through router '" + routerChannelId
                                    + "': " + reply.result()));
                            return;
                        }
                        List<Message> replyParts = ZLinkChannelRuntime.copyMessages(reply.parts());
                        if (ZLinkChannelRuntime.isFrameworkErrorReply(replyParts)) {
                            replyParts.forEach(Message::close);
                            result.completeExceptionally(new ZLinkFrameworkException(
                                ZLinkFrameworkErrorKind.REQUEST_FAILED,
                                ZLinkChannelRuntime.frameworkErrorReplyMessage(reply.parts())));
                            return;
                        }
                        if (!result.complete(replyParts)) {
                            replyParts.forEach(Message::close);
                        }
                    } finally {
                        reply.close();
                    }
                },
                SendFlags.NONE,
                timeout);
            ZLinkChannelRuntime.trace("spot-route node-submit router=" + routerChannelId
                + " targetNode=" + targetNodeRid
                + " targetSpot=" + targetSpotRid
                + " submitted=" + submitted);
            if (!submitted) {
                result.completeExceptionally(new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.REQUEST_FAILED,
                    "Spot node router '" + routerChannelId + "' is not ready for SPOT request."));
            }
        } catch (RuntimeException ex) {
            result.completeExceptionally(ex);
        } finally {
            requestParts.forEach(Message::close);
        }
        return result;
    }
}
