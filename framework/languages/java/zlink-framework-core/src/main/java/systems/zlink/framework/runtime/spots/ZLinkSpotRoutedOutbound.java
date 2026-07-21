package systems.zlink.framework.runtime.spots;

import java.time.Duration;
import java.util.List;
import java.util.Map;
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
import systems.zlink.framework.runtime.messaging.ZLinkApplicationMetadata;
import systems.zlink.framework.runtime.messaging.ZLinkSubmitResults;

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
        long spotGeneration,
        Message payload,
        Optional<String> packetName) {
        return new ZLinkSpotRoutedSendCall(
            this,
            routerChannelId,
            targetNodeRid,
            spotRid,
            spotGeneration,
            payload,
            packetName);
    }

    ZLinkRequestCall request(
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        long spotGeneration,
        Message payload,
        Optional<String> packetName,
        Duration timeout) {
        return new ZLinkSpotRoutedRequestCall(
            this,
            routerChannelId,
            targetNodeRid,
            spotRid,
            spotGeneration,
            payload,
            packetName,
            timeout);
    }

    CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> submitSend(
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        long spotGeneration,
        Message payload,
        Optional<String> packetName,
        ZLinkApplicationMetadata metadata) {
        trace(
            ZLinkMessageFlowOutcome.SENT,
            ZLinkDispatchMessageKind.SEND,
            packetName,
            routerChannelId,
            spotRid);
        ZLinkInternalSpotNode routerNode = localRouterNode.apply(routerChannelId);
        if (routerNode != null) {
            return directOutbound.send(
                routerNode.entrySpot(),
                targetNodeRid,
                spotRid,
                spotGeneration,
                payload,
                packetName)
                .metadata(metadata.values())
                .submit();
        }
        if (!metadata.values().isEmpty()) {
            throw new UnsupportedOperationException(
                "legacy routed Spot transport does not support application metadata");
        }
        List<Message> parts = messages.encode(packetName, payload);
        try {
            return ZLinkSubmitResults.fromVoidStage(channels.sendToSpotViaRouterChannel(
                routerChannelId,
                targetNodeRid,
                spotRid,
                spotGeneration,
                parts).whenComplete((ignored, error) -> {
                    parts.forEach(Message::close);
                    if (error != null) {
                        java.util.logging.Logger.getLogger(ZLinkSpotRoutedOutbound.class.getName())
                            .log(java.util.logging.Level.SEVERE, "one-way routed SPOT submission failed", error);
                    }
                }));
        } catch (RuntimeException error) {
            parts.forEach(Message::close);
            throw error;
        }
    }

    <TReply> CompletionStage<TReply> submitRequest(
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        long spotGeneration,
        Message payload,
        Optional<String> packetName,
        ZLinkApplicationMetadata metadata,
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
                spotGeneration,
                payload,
                packetName,
                timeout)
                .metadata(metadata.values())
                .submit(replyType);
        }
        if (!metadata.values().isEmpty()) {
            throw new UnsupportedOperationException(
                "legacy routed Spot transport does not support application metadata");
        }
        List<Message> parts = messages.encode(packetName, payload);
        try {
                return channels.requestToSpotViaRouterChannel(
                    routerChannelId,
                    targetNodeRid,
                    spotRid,
                    spotGeneration,
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
    private final systems.zlink.framework.runtime.messaging.ZLinkOneWayCallGate submitGate =
        new systems.zlink.framework.runtime.messaging.ZLinkOneWayCallGate();
    private final ZLinkSpotRoutedOutbound outbound;
    private final String routerChannelId;
    private final RoutingId targetNodeRid;
    private final RoutingId spotRid;
    private final long spotGeneration;
    private final Message payload;
    private final Optional<String> packetName;
    private final ZLinkApplicationMetadata metadata;

    ZLinkSpotRoutedSendCall(
        ZLinkSpotRoutedOutbound outbound,
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        long spotGeneration,
        Message payload,
        Optional<String> packetName) {
        this(
            outbound,
            routerChannelId,
            targetNodeRid,
            spotRid,
            spotGeneration,
            payload,
            packetName,
            ZLinkApplicationMetadata.empty());
    }

    private ZLinkSpotRoutedSendCall(
        ZLinkSpotRoutedOutbound outbound,
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        long spotGeneration,
        Message payload,
        Optional<String> packetName,
        ZLinkApplicationMetadata metadata) {
        this.outbound = outbound;
        this.routerChannelId = routerChannelId;
        this.targetNodeRid = targetNodeRid;
        this.spotRid = spotRid;
        this.spotGeneration = spotGeneration;
        this.payload = payload;
        this.packetName = packetName;
        this.metadata = metadata;
    }

    public ZLinkSendCall packetName(String packetName) {
        return new ZLinkSpotRoutedSendCall(
            outbound,
            routerChannelId,
            targetNodeRid,
            spotRid,
            spotGeneration,
            payload,
            Optional.of(packetName),
            metadata);
    }

    @Override
    public ZLinkSendCall metadata(String key, String value) {
        return new ZLinkSpotRoutedSendCall(
            outbound,
            routerChannelId,
            targetNodeRid,
            spotRid,
            spotGeneration,
            payload,
            packetName,
            metadata.with(key, value));
    }

    @Override
    public ZLinkSendCall metadata(Map<String, String> values) {
        return new ZLinkSpotRoutedSendCall(
            outbound,
            routerChannelId,
            targetNodeRid,
            spotRid,
            spotGeneration,
            payload,
            packetName,
            metadata.withAll(values));
    }

    @Override
    public CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> submit() {
        CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> duplicate =
            submitGate.begin();
        if (duplicate != null) {
            return duplicate;
        }
        return outbound.submitSend(
            routerChannelId,
            targetNodeRid,
            spotRid,
            spotGeneration,
            payload,
            packetName,
            metadata);
    }
}

final class ZLinkSpotRoutedRequestCall implements ZLinkRequestCall {
    private final ZLinkSpotRoutedOutbound outbound;
    private final String routerChannelId;
    private final RoutingId targetNodeRid;
    private final RoutingId spotRid;
    private final long spotGeneration;
    private final Message payload;
    private final Optional<String> packetName;
    private final Duration timeout;
    private final ZLinkApplicationMetadata metadata;

    ZLinkSpotRoutedRequestCall(
        ZLinkSpotRoutedOutbound outbound,
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        long spotGeneration,
        Message payload,
        Optional<String> packetName,
        Duration timeout) {
        this(
            outbound,
            routerChannelId,
            targetNodeRid,
            spotRid,
            spotGeneration,
            payload,
            packetName,
            timeout,
            ZLinkApplicationMetadata.empty());
    }

    private ZLinkSpotRoutedRequestCall(
        ZLinkSpotRoutedOutbound outbound,
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        long spotGeneration,
        Message payload,
        Optional<String> packetName,
        Duration timeout,
        ZLinkApplicationMetadata metadata) {
        this.outbound = outbound;
        this.routerChannelId = routerChannelId;
        this.targetNodeRid = targetNodeRid;
        this.spotRid = spotRid;
        this.spotGeneration = spotGeneration;
        this.payload = payload;
        this.packetName = packetName;
        this.timeout = timeout;
        this.metadata = metadata;
    }

    public ZLinkRequestCall packetName(String packetName) {
        return new ZLinkSpotRoutedRequestCall(
            outbound,
            routerChannelId,
            targetNodeRid,
            spotRid,
            spotGeneration,
            payload,
            Optional.of(packetName),
            timeout,
            metadata);
    }

    @Override
    public ZLinkRequestCall metadata(String key, String value) {
        return new ZLinkSpotRoutedRequestCall(
            outbound,
            routerChannelId,
            targetNodeRid,
            spotRid,
            spotGeneration,
            payload,
            packetName,
            timeout,
            metadata.with(key, value));
    }

    @Override
    public ZLinkRequestCall metadata(Map<String, String> values) {
        return new ZLinkSpotRoutedRequestCall(
            outbound,
            routerChannelId,
            targetNodeRid,
            spotRid,
            spotGeneration,
            payload,
            packetName,
            timeout,
            metadata.withAll(values));
    }

    @Override
    public ZLinkRequestCall timeout(Duration timeout) {
        return new ZLinkSpotRoutedRequestCall(
            outbound,
            routerChannelId,
            targetNodeRid,
            spotRid,
            spotGeneration,
            payload,
            packetName,
            timeout,
            metadata);
    }

    @Override
    public <TReply> CompletionStage<TReply> submit(Class<TReply> replyType) {
        return systems.zlink.framework.execution.ZLinkAsyncSerialQueue.manageCurrent(
            outbound.submitRequest(
            routerChannelId,
            targetNodeRid,
            spotRid,
            spotGeneration,
            payload,
            packetName,
            metadata,
            timeout,
            replyType));
    }

}
