package systems.zlink.framework.runtime.spots;

import java.time.Duration;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.channels.ZLinkRequestCall;
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface;
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind;
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent;
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer;

final class ZLinkSpotRoutedOutbound {
    private final ZLinkChannelRuntime channels;
    private final ZLinkSpotRouteMessages messages;
    private final ZLinkSpotDirectOutbound directOutbound;
    private final ZLinkMessageFlowTracer flow;
    private final Function<String, ZLinkInternalSpotNode> localRouterNode;

    ZLinkSpotRoutedOutbound(
        ZLinkChannelRuntime channels,
        ZLinkSpotRouteMessages messages,
        ZLinkSpotDirectOutbound directOutbound,
        ZLinkMessageFlowTracer flow,
        Function<String, ZLinkInternalSpotNode> localRouterNode) {
        this.channels = channels;
        this.messages = messages;
        this.directOutbound = directOutbound;
        this.flow = flow;
        this.localRouterNode = localRouterNode;
    }

    ZLinkSendCall send(
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        Message payload,
        Optional<String> packetName) {
        return new ZLinkSpotRoutedSendCall(
            this,
            routerChannelId,
            targetNodeRid,
            spotRid,
            payload,
            packetName);
    }

    ZLinkRequestCall request(
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        Message payload,
        Optional<String> packetName,
        Duration timeout) {
        return new ZLinkSpotRoutedRequestCall(
            this,
            routerChannelId,
            targetNodeRid,
            spotRid,
            payload,
            packetName,
            timeout);
    }

    void submitSend(
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        Message payload,
        Optional<String> packetName) {
        trace(
            ZLinkMessageFlowOutcome.SENT,
            ZLinkDispatchMessageKind.SEND,
            packetName,
            routerChannelId,
            spotRid);
        ZLinkInternalSpotNode routerNode = localRouterNode.apply(routerChannelId);
        if (routerNode != null) {
            directOutbound.send(
                routerNode.entrySpot(),
                targetNodeRid,
                spotRid,
                payload,
                packetName).submit();
            return;
        }
        List<Message> parts = messages.encode(packetName, payload);
        try {
            channels.sendToSpotViaRouterChannel(
                routerChannelId,
                targetNodeRid,
                spotRid,
                parts).whenComplete((ignored, error) -> {
                    parts.forEach(Message::close);
                    if (error != null) {
                        java.util.logging.Logger.getLogger(ZLinkSpotRoutedOutbound.class.getName())
                            .log(java.util.logging.Level.SEVERE, "one-way routed SPOT submission failed", error);
                    }
                });
        } catch (RuntimeException error) {
            parts.forEach(Message::close);
            throw error;
        }
    }

    <TReply> CompletionStage<TReply> submitRequest(
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        Message payload,
        Optional<String> packetName,
        Duration timeout,
        Class<TReply> replyType) {
        trace(
            ZLinkMessageFlowOutcome.SENT,
            ZLinkDispatchMessageKind.REQUEST,
            packetName,
            routerChannelId,
            spotRid);
        ZLinkInternalSpotNode routerNode = localRouterNode.apply(routerChannelId);
        if (routerNode != null) {
            return directOutbound.request(
                routerNode.entrySpot(),
                targetNodeRid,
                spotRid,
                payload,
                packetName,
                timeout).submit(replyType);
        }
        List<Message> parts = messages.encode(packetName, payload);
        try {
            return channels.requestToSpotViaRouterChannel(
                    routerChannelId,
                    targetNodeRid,
                    spotRid,
                    parts,
                    timeout)
                .thenApply(replyParts -> {
                    trace(
                        ZLinkMessageFlowOutcome.REPLY_RECEIVED,
                        ZLinkDispatchMessageKind.RESPONSE,
                        packetName,
                        routerChannelId,
                        spotRid);
                    return messages.decodeReply(replyParts, replyType);
                });
        } finally {
            parts.forEach(Message::close);
        }
    }

    private void trace(
        ZLinkMessageFlowOutcome outcome,
        ZLinkDispatchMessageKind kind,
        Optional<String> packetName,
        String routerChannelId,
        RoutingId spotRid) {
        if (!flow.enabled(outcome)) {
            return;
        }
        flow.trace(new ZLinkMessageFlowEvent(
            outcome,
            ZLinkDispatchErrorSurface.SPOT_ROUTE,
            kind,
            packetName.orElse(null),
            routerChannelId,
            null,
            null,
            null,
            spotRid.toString(),
            null,
            null));
    }
}

final class ZLinkSpotRoutedSendCall implements ZLinkSendCall {
    private final ZLinkSpotRoutedOutbound outbound;
    private final String routerChannelId;
    private final RoutingId targetNodeRid;
    private final RoutingId spotRid;
    private final Message payload;
    private final Optional<String> packetName;

    ZLinkSpotRoutedSendCall(
        ZLinkSpotRoutedOutbound outbound,
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        Message payload,
        Optional<String> packetName) {
        this.outbound = outbound;
        this.routerChannelId = routerChannelId;
        this.targetNodeRid = targetNodeRid;
        this.spotRid = spotRid;
        this.payload = payload;
        this.packetName = packetName;
    }

    public ZLinkSendCall packetName(String packetName) {
        return new ZLinkSpotRoutedSendCall(
            outbound,
            routerChannelId,
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
            routerChannelId,
            targetNodeRid,
            spotRid,
            payload,
            packetName);
    }
}

final class ZLinkSpotRoutedRequestCall implements ZLinkRequestCall {
    private final ZLinkSpotRoutedOutbound outbound;
    private final String routerChannelId;
    private final RoutingId targetNodeRid;
    private final RoutingId spotRid;
    private final Message payload;
    private final Optional<String> packetName;
    private final Duration timeout;

    ZLinkSpotRoutedRequestCall(
        ZLinkSpotRoutedOutbound outbound,
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        Message payload,
        Optional<String> packetName,
        Duration timeout) {
        this.outbound = outbound;
        this.routerChannelId = routerChannelId;
        this.targetNodeRid = targetNodeRid;
        this.spotRid = spotRid;
        this.payload = payload;
        this.packetName = packetName;
        this.timeout = timeout;
    }

    public ZLinkRequestCall packetName(String packetName) {
        return new ZLinkSpotRoutedRequestCall(
            outbound,
            routerChannelId,
            targetNodeRid,
            spotRid,
            payload,
            Optional.of(packetName),
            timeout);
    }

    @Override
    public ZLinkRequestCall metadata(String key, String value) {
        return this;
    }

    @Override
    public ZLinkRequestCall timeout(Duration timeout) {
        return new ZLinkSpotRoutedRequestCall(
            outbound,
            routerChannelId,
            targetNodeRid,
            spotRid,
            payload,
            packetName,
            timeout);
    }

    @Override
    public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
        return systems.zlink.framework.execution.ZLinkAsyncSerialQueue.manageCurrent(
            outbound.submitRequest(
            routerChannelId,
            targetNodeRid,
            spotRid,
            payload,
            packetName,
            timeout,
            replyType));
    }

}
