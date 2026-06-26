package systems.zlink.framework.runtime.actors;

import java.time.Duration;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkAwait;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkBoundSession;
import systems.zlink.framework.actors.ZLinkBoundSessionSendCall;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.execution.ZLinkFrameworkTurns;
import systems.zlink.framework.execution.ZLinkYieldTurn;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.runtime.streams.ZLinkStreamFrameCodec;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderFlag;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.errors.ZLinkConfigurationException;

final class ZLinkRoutedBoundSessionRuntime implements ZLinkBoundSession {
    private final ZLinkBackendSpot sourceEntrySpot;
    private final ZLinkChannelRuntime routedTransport;
    private final String routeChannelName;
    private final RoutingId targetNodeRid;
    private final RoutingId targetEntrySpotRid;
    private final ZLinkBackendActorRef actorRef;
    private final ZLinkMessageSerializer serializer;
    private final ZLinkActorRuntime actorRuntime;
    private final ZLinkActor actor;
    private final Duration timeout;
    private final ZLinkStreamCodec defaultCodec;
    private long bindingToken;

    ZLinkRoutedBoundSessionRuntime(
        ZLinkBackendSpot sourceEntrySpot,
        ZLinkChannelRuntime routedTransport,
        String routeChannelName,
        RoutingId targetNodeRid,
        RoutingId targetEntrySpotRid,
        ZLinkBackendActorRef actorRef,
        ZLinkMessageSerializer serializer,
        ZLinkActorRuntime actorRuntime,
        ZLinkActor actor,
        Duration timeout,
        ZLinkStreamCodec defaultCodec) {
        this.sourceEntrySpot = sourceEntrySpot;
        this.routedTransport = routedTransport;
        this.routeChannelName = routeChannelName;
        this.targetNodeRid = targetNodeRid;
        this.targetEntrySpotRid = targetEntrySpotRid;
        this.actorRef = actorRef;
        this.serializer = serializer;
        this.actorRuntime = actorRuntime;
        this.actor = actor;
        this.timeout = timeout;
        this.defaultCodec = defaultCodec == null ? ZLinkStreamCodec.JSON : defaultCodec;
    }

    void setBindingToken(long bindingToken) {
        this.bindingToken = bindingToken;
    }

    @Override
    public ZLinkBoundSessionSendCall send(Object message) {
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, message);
        return new SendCall(
            sourceEntrySpot,
            routedTransport,
            routeChannelName,
            targetNodeRid,
            targetEntrySpotRid,
            actorRef,
            encoded.payload(),
            encoded.packetName(),
            Map.of(),
            Optional.empty(),
            timeout,
            defaultCodec,
            ZLinkFrameworkTurns.captureCurrent());
    }

    CompletionStage<Void> sendFrame(byte[] frameBytes) {
        try (Message frame = Message.from(frameBytes)) {
            return sendFrame(
                sourceEntrySpot,
                routedTransport,
                routeChannelName,
                targetNodeRid,
                targetEntrySpotRid,
                actorRef,
                frame);
        }
    }

    @Override
    public CompletionStage<Void> disconnect() {
        actorRuntime.clearSessionBinding(actor, bindingToken);
        return java.util.concurrent.CompletableFuture.completedFuture(null);
    }

    private static CompletionStage<Void> sendFrame(
        ZLinkBackendSpot sourceEntrySpot,
        ZLinkChannelRuntime routedTransport,
        String routeChannelName,
        RoutingId targetNodeRid,
        RoutingId targetEntrySpotRid,
        ZLinkBackendActorRef actorRef,
        Message frame) {
        List<Message> parts = ZLinkActorSpotRoutePackets.createBoundSessionSendParts(actorRef, frame);
        try {
            if (routedTransport != null && routeChannelName != null && !routeChannelName.isBlank()) {
                return routedTransport.sendToSpotViaRouterChannel(
                    routeChannelName,
                    targetNodeRid,
                    targetEntrySpotRid,
                    parts);
            }
            boolean submitted = sourceEntrySpot.sendToSpot(
                    targetNodeRid,
                    targetEntrySpotRid,
                    parts,
                    SendFlags.NONE);
            if (!submitted) {
                return java.util.concurrent.CompletableFuture.failedFuture(
                    new ZLinkConfigurationException(
                        "routed actor bound session target is not ready"));
            }
            return java.util.concurrent.CompletableFuture.completedFuture(null);
        } finally {
            parts.forEach(Message::close);
        }
    }

    private record SendCall(
        ZLinkBackendSpot sourceEntrySpot,
        ZLinkChannelRuntime routedTransport,
        String routeChannelName,
        RoutingId targetNodeRid,
        RoutingId targetEntrySpotRid,
        ZLinkBackendActorRef actorRef,
        Message payload,
        String defaultPacketName,
        Map<String, String> metadata,
        Optional<String> packetName,
        Duration timeout,
        ZLinkStreamCodec codec,
        ZLinkYieldTurn turn) implements ZLinkBoundSessionSendCall {
        @Override
        public ZLinkBoundSessionSendCall packetName(String packetName) {
            if (packetName == null || packetName.isBlank()) {
                throw new IllegalArgumentException("packetName is required");
            }
            return new SendCall(
                sourceEntrySpot,
                routedTransport,
                routeChannelName,
                targetNodeRid,
                targetEntrySpotRid,
                actorRef,
                payload,
                defaultPacketName,
                metadata,
                Optional.of(packetName),
                timeout,
                codec,
                turn);
        }

        @Override
        public ZLinkBoundSessionSendCall metadata(String key, String value) {
            Map<String, String> next = new HashMap<>(metadata);
            next.put(key, value);
            return new SendCall(
                sourceEntrySpot,
                routedTransport,
                routeChannelName,
                targetNodeRid,
                targetEntrySpotRid,
                actorRef,
                payload,
                defaultPacketName,
                Map.copyOf(next),
                packetName,
                timeout,
                codec,
                turn);
        }

        @Override
        public CompletionStage<Void> submit() {
            byte[] frameBytes;
            try {
                ZLinkStreamHeader header = new ZLinkStreamHeader(
                    ZLinkStreamMessageKind.SEND,
                    codec,
                    EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
                    Optional.empty(),
                    packetName.orElse(defaultPacketName),
                    metadata);
                frameBytes = ZLinkStreamFrameCodec.encode(header, payload.toByteArray());
            } finally {
                payload.close();
            }
            try (Message frame = Message.from(frameBytes)) {
                return ZLinkRoutedBoundSessionRuntime.sendFrame(
                    sourceEntrySpot,
                    routedTransport,
                    routeChannelName,
                    targetNodeRid,
                    targetEntrySpotRid,
                    actorRef,
                    frame);
            }
        }

        @Override
        public void yield() {
            ZLinkAwait.awaitVoid(
                ZLinkFrameworkTurns.awaitManagedCompletion(requireTurn(), submit()));
        }

        private ZLinkYieldTurn requireTurn() {
            if (turn == null) {
                throw new IllegalStateException(
                    "yield requires a framework Spot handler turn captured when the call object was created");
            }
            return turn;
        }
    }

}
