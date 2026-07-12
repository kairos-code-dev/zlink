package systems.zlink.framework.runtime.spots;

import java.time.Duration;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.Executor;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.ZLinkAwait;
import systems.zlink.framework.channels.ZLinkPublishCall;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.channels.ZLinkYieldRequestCall;
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface;
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind;
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent;
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome;
import systems.zlink.framework.execution.ZLinkFrameworkTurns;
import systems.zlink.framework.execution.ZLinkYieldTurn;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer;

final class ZLinkSpotDirectOutbound {
    private final ZLinkSpotRouteMessages messages;
    private final Executor handlerExecutor;
    private final ZLinkMessageFlowTracer flow;

    ZLinkSpotDirectOutbound(
        ZLinkSpotRouteMessages messages,
        Executor handlerExecutor,
        ZLinkMessageFlowTracer flow) {
        this.messages = messages;
        this.handlerExecutor = handlerExecutor;
        this.flow = flow;
    }

    ZLinkSendCall send(
        ZLinkBackendSpot spot,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        Message payload,
        Optional<String> packetName) {
        return new ZLinkSpotDirectSendCall(
            this, spot, targetNodeRid, spotRid, payload, packetName);
    }

    ZLinkYieldRequestCall request(
        ZLinkBackendSpot spot,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        Message payload,
        Optional<String> packetName,
        Duration timeout) {
        return request(
            spot,
            targetNodeRid,
            spotRid,
            payload,
            packetName,
            timeout,
            ZLinkFrameworkTurns.captureCurrent());
    }

    ZLinkYieldRequestCall request(
        ZLinkBackendSpot spot,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        Message payload,
        Optional<String> packetName,
        Duration timeout,
        ZLinkYieldTurn turn) {
        return new ZLinkSpotDirectRequestCall(
            this,
            spot,
            targetNodeRid,
            spotRid,
            payload,
            packetName,
            timeout,
            turn);
    }

    ZLinkPublishCall publish(
        ZLinkBackendSpot spot,
        String topic,
        Message payload,
        Optional<String> packetName) {
        return new ZLinkSpotDirectPublishCall(this, spot, topic, payload, packetName);
    }

    void submitSend(
        ZLinkBackendSpot spot,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        Message payload,
        Optional<String> packetName) {
        trace(
            ZLinkMessageFlowOutcome.SENT,
            ZLinkDispatchMessageKind.SEND,
            packetName,
            null,
            targetNodeRid,
            spotRid);
        settle(CompletableFuture.runAsync(() -> {
            List<Message> parts = messages.encode(packetName, payload);
            try {
                spot.sendToSpot(targetNodeRid, spotRid, parts, SendFlags.NONE);
            } finally {
                parts.forEach(Message::close);
            }
        }, handlerExecutor));
    }

    <TReply> CompletionStage<TReply> submitRequest(
        ZLinkBackendSpot spot,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        Message payload,
        Optional<String> packetName,
        Duration timeout,
        Class<TReply> replyType) {
        CompletableFuture<TReply> result = new CompletableFuture<>();
        List<Message> requestParts = messages.encode(packetName, payload);
        trace(
            ZLinkMessageFlowOutcome.SENT,
            ZLinkDispatchMessageKind.REQUEST,
            packetName,
            null,
            targetNodeRid,
            spotRid);
        try {
            spot.requestToSpot(targetNodeRid, spotRid, requestParts, reply -> {
                trace(
                    ZLinkMessageFlowOutcome.REPLY_RECEIVED,
                    ZLinkDispatchMessageKind.RESPONSE,
                    packetName,
                    null,
                    targetNodeRid,
                    spotRid);
                try {
                    result.complete(messages.decodeReply(reply.parts(), replyType));
                } catch (RuntimeException ex) {
                    result.completeExceptionally(ex);
                }
            }, SendFlags.NONE, timeout);
        } catch (RuntimeException ex) {
            result.completeExceptionally(ex);
        } finally {
            requestParts.forEach(Message::close);
        }
        return result.thenApplyAsync(reply -> reply, handlerExecutor);
    }

    void submitPublish(
        ZLinkBackendSpot spot,
        String topic,
        Message payload,
        Optional<String> packetName) {
        trace(
            ZLinkMessageFlowOutcome.SENT,
            ZLinkDispatchMessageKind.PUBLISH,
            packetName,
            topic,
            null,
            null);
        settle(CompletableFuture.runAsync(() -> {
            List<Message> parts = messages.encode(packetName, payload);
            try {
                spot.publish(topic, parts, SendFlags.NONE);
            } finally {
                parts.forEach(Message::close);
            }
        }, handlerExecutor));
    }

    private void settle(CompletionStage<Void> submission) {
        submission.exceptionally(error -> {
            java.util.logging.Logger.getLogger(ZLinkSpotDirectOutbound.class.getName())
                .log(java.util.logging.Level.SEVERE, "one-way SPOT submission failed", error);
            return null;
        });
    }

    private void trace(
        ZLinkMessageFlowOutcome outcome,
        ZLinkDispatchMessageKind kind,
        Optional<String> packetName,
        String topic,
        RoutingId targetNodeRid,
        RoutingId spotRid) {
        if (!flow.enabled(outcome)) {
            return;
        }
        flow.trace(new ZLinkMessageFlowEvent(
            outcome,
            topic == null
                ? ZLinkDispatchErrorSurface.SPOT_ROUTE
                : ZLinkDispatchErrorSurface.SPOT_SUBSCRIPTION,
            kind,
            packetName.orElse(null),
            null,
            topic,
            null,
            targetNodeRid == null ? null : targetNodeRid.toString(),
            spotRid == null ? null : spotRid.toString(),
            null,
            null));
    }
}

final class ZLinkSpotDirectSendCall implements ZLinkSendCall {
    private final ZLinkSpotDirectOutbound outbound;
    private final ZLinkBackendSpot spot;
    private final RoutingId targetNodeRid;
    private final RoutingId spotRid;
    private final Message payload;
    private final Optional<String> packetName;

    ZLinkSpotDirectSendCall(
        ZLinkSpotDirectOutbound outbound,
        ZLinkBackendSpot spot,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        Message payload,
        Optional<String> packetName) {
        this.outbound = outbound;
        this.spot = spot;
        this.targetNodeRid = targetNodeRid;
        this.spotRid = spotRid;
        this.payload = payload;
        this.packetName = packetName;
    }

    public ZLinkSendCall packetName(String packetName) {
        return new ZLinkSpotDirectSendCall(
            outbound,
            spot,
            targetNodeRid,
            spotRid,
            payload,
            Optional.of(packetName));
    }

    @Override
    public ZLinkSendCall metadata(String key, String value) {
        return this;
    }

    @Override
    public void submit() {
        outbound.submitSend(
            spot, targetNodeRid, spotRid, payload, packetName);
    }
}

final class ZLinkSpotDirectRequestCall implements ZLinkYieldRequestCall {
    private final ZLinkSpotDirectOutbound outbound;
    private final ZLinkBackendSpot spot;
    private final RoutingId targetNodeRid;
    private final RoutingId spotRid;
    private final Message payload;
    private final Optional<String> packetName;
    private final Duration timeout;
    private final ZLinkYieldTurn turn;

    ZLinkSpotDirectRequestCall(
        ZLinkSpotDirectOutbound outbound,
        ZLinkBackendSpot spot,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        Message payload,
        Optional<String> packetName,
        Duration timeout,
        ZLinkYieldTurn turn) {
        this.outbound = outbound;
        this.spot = spot;
        this.targetNodeRid = targetNodeRid;
        this.spotRid = spotRid;
        this.payload = payload;
        this.packetName = packetName;
        this.timeout = timeout;
        this.turn = turn;
    }

    public ZLinkYieldRequestCall packetName(String packetName) {
        return new ZLinkSpotDirectRequestCall(
            outbound,
            spot,
            targetNodeRid,
            spotRid,
            payload,
            Optional.of(packetName),
            timeout,
            turn);
    }

    @Override
    public ZLinkYieldRequestCall metadata(String key, String value) {
        return this;
    }

    @Override
    public ZLinkYieldRequestCall timeout(Duration timeout) {
        return new ZLinkSpotDirectRequestCall(
            outbound,
            spot,
            targetNodeRid,
            spotRid,
            payload,
            packetName,
            timeout,
            turn);
    }

    @Override
    public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
        return outbound.submitRequest(
            spot,
            targetNodeRid,
            spotRid,
            payload,
            packetName,
            timeout,
            replyType);
    }

    @Override
    public <TReply> TReply yield(Class<TReply> replyType) {
        return ZLinkAwait.await(
            ZLinkFrameworkTurns.awaitManagedCompletion(requireTurn(), submit(replyType)));
    }

    private ZLinkYieldTurn requireTurn() {
        if (turn != null) {
            return turn;
        }
        ZLinkYieldTurn current = ZLinkFrameworkTurns.captureCurrent();
        if (current != null) {
            return current;
        }
        throw new IllegalStateException(
            "yield requires a framework Spot handler turn captured when the call object was created");
    }
}

final class ZLinkSpotDirectPublishCall implements ZLinkPublishCall {
    private final ZLinkSpotDirectOutbound outbound;
    private final ZLinkBackendSpot spot;
    private final String topic;
    private final Message payload;
    private final Optional<String> packetName;

    ZLinkSpotDirectPublishCall(
        ZLinkSpotDirectOutbound outbound,
        ZLinkBackendSpot spot,
        String topic,
        Message payload,
        Optional<String> packetName) {
        this.outbound = outbound;
        this.spot = spot;
        this.topic = topic;
        this.payload = payload;
        this.packetName = packetName;
    }

    public ZLinkPublishCall packetName(String packetName) {
        return new ZLinkSpotDirectPublishCall(
            outbound, spot, topic, payload, Optional.of(packetName));
    }

    @Override
    public ZLinkPublishCall metadata(String key, String value) {
        return this;
    }

    @Override
    public void submit() {
        outbound.submitPublish(spot, topic, payload, packetName);
    }
}
