package systems.zlink.framework.runtime.spots;

import java.time.Duration;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletionStage;
import java.util.function.Function;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.ZLinkAwait;
import systems.zlink.framework.ZLinkSubmitStage;
import systems.zlink.framework.channels.ZLinkSendCall;
import systems.zlink.framework.channels.ZLinkYieldRequestCall;
import systems.zlink.framework.configuration.ZLinkDispatchErrorSurface;
import systems.zlink.framework.configuration.ZLinkDispatchMessageKind;
import systems.zlink.framework.configuration.ZLinkMessageFlowEvent;
import systems.zlink.framework.configuration.ZLinkMessageFlowOutcome;
import systems.zlink.framework.execution.ZLinkFrameworkTurns;
import systems.zlink.framework.execution.ZLinkYieldTurn;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNode;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.diagnostics.ZLinkMessageFlowTracer;

final class ZLinkSpotRoutedOutbound {
    private final ZLinkChannelRuntime channels;
    private final ZLinkSpotRouteMessages messages;
    private final ZLinkSpotDirectOutbound directOutbound;
    private final ZLinkMessageFlowTracer flow;
    private final Function<String, ZLinkBackendSpotNode> localRouterNode;

    ZLinkSpotRoutedOutbound(
        ZLinkChannelRuntime channels,
        ZLinkSpotRouteMessages messages,
        ZLinkSpotDirectOutbound directOutbound,
        ZLinkMessageFlowTracer flow,
        Function<String, ZLinkBackendSpotNode> localRouterNode) {
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

    ZLinkYieldRequestCall request(
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
            timeout,
            ZLinkFrameworkTurns.captureCurrent());
    }

    ZLinkSubmitStage submitSend(
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
        ZLinkBackendSpotNode routerNode = localRouterNode.apply(routerChannelId);
        if (routerNode != null) {
            return directOutbound.send(
                routerNode.entrySpot(),
                targetNodeRid,
                spotRid,
                payload,
                packetName).submit();
        }
        List<Message> parts = messages.encode(packetName, payload);
        try {
            return ZLinkSubmitStage.from(channels.sendToSpotViaRouterChannel(
                routerChannelId,
                targetNodeRid,
                spotRid,
                parts));
        } finally {
            parts.forEach(Message::close);
        }
    }

    <TReply> CompletionStage<TReply> submitRequest(
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        Message payload,
        Optional<String> packetName,
        Duration timeout,
        ZLinkYieldTurn turn,
        Class<TReply> replyType) {
        trace(
            ZLinkMessageFlowOutcome.SENT,
            ZLinkDispatchMessageKind.REQUEST,
            packetName,
            routerChannelId,
            spotRid);
        ZLinkBackendSpotNode routerNode = localRouterNode.apply(routerChannelId);
        if (routerNode != null) {
            return directOutbound.request(
                routerNode.entrySpot(),
                targetNodeRid,
                spotRid,
                payload,
                packetName,
                timeout,
                turn).submit(replyType);
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

    @Override
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
    public ZLinkSubmitStage submit() {
        return outbound.submitSend(
            routerChannelId,
            targetNodeRid,
            spotRid,
            payload,
            packetName);
    }
}

final class ZLinkSpotRoutedRequestCall implements ZLinkYieldRequestCall {
    private final ZLinkSpotRoutedOutbound outbound;
    private final String routerChannelId;
    private final RoutingId targetNodeRid;
    private final RoutingId spotRid;
    private final Message payload;
    private final Optional<String> packetName;
    private final Duration timeout;
    private final ZLinkYieldTurn turn;

    ZLinkSpotRoutedRequestCall(
        ZLinkSpotRoutedOutbound outbound,
        String routerChannelId,
        RoutingId targetNodeRid,
        RoutingId spotRid,
        Message payload,
        Optional<String> packetName,
        Duration timeout,
        ZLinkYieldTurn turn) {
        this.outbound = outbound;
        this.routerChannelId = routerChannelId;
        this.targetNodeRid = targetNodeRid;
        this.spotRid = spotRid;
        this.payload = payload;
        this.packetName = packetName;
        this.timeout = timeout;
        this.turn = turn;
    }

    @Override
    public ZLinkYieldRequestCall packetName(String packetName) {
        return new ZLinkSpotRoutedRequestCall(
            outbound,
            routerChannelId,
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
        return new ZLinkSpotRoutedRequestCall(
            outbound,
            routerChannelId,
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
            routerChannelId,
            targetNodeRid,
            spotRid,
            payload,
            packetName,
            timeout,
            turn,
            replyType);
    }

    @Override
    public <TReply> TReply yield(Class<TReply> replyType) {
        return ZLinkAwait.await(
            ZLinkFrameworkTurns.awaitManagedCompletion(requireTurn(), submit(replyType)));
    }

    @Override
    public <TReply> TReply yield(
        Class<TReply> replyType,
        CancellationToken cancellationToken) {
        ZLinkFrameworkTurns.throwIfCancellationRequested(cancellationToken);
        return ZLinkAwait.await(ZLinkFrameworkTurns.awaitManagedCompletion(
            requireTurn(),
            submit(replyType),
            cancellationToken));
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
