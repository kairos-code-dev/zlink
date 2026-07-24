package systems.zlink.framework.runtime.actors;

import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkBoundSession;
import systems.zlink.framework.actors.ZLinkBoundSessionSendCall;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.channels.ZLinkChannelRuntime;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.runtime.messaging.ZLinkSubmitResults;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.errors.ZLinkConfigurationException;

final class ZLinkRoutedBoundSessionRuntime implements ZLinkBoundSession {
    private final ZLinkBackendSpot sourceEntrySpot;
    private final ZLinkChannelRuntime routedTransport;
    private final String routeChannelName;
    private final RoutingId targetNodeRid;
    private final String targetEntrySpotId;
    private final ZLinkBackendActorRef actorRef;
    private final ZLinkMessageSerializer serializer;
    private final ZLinkActorRuntime actorRuntime;
    private final ZLinkActor actor;
    private final Duration timeout;
    private final ZLinkStreamCodec defaultCodec;
    private final ZLinkRelayMetadataPolicy metadataPolicy;
    private long bindingToken;

    ZLinkRoutedBoundSessionRuntime(
        ZLinkBackendSpot sourceEntrySpot,
        ZLinkChannelRuntime routedTransport,
        String routeChannelName,
        RoutingId targetNodeRid,
        String targetEntrySpotId,
        ZLinkBackendActorRef actorRef,
        ZLinkMessageSerializer serializer,
        ZLinkActorRuntime actorRuntime,
        ZLinkActor actor,
        Duration timeout,
        ZLinkStreamCodec defaultCodec,
        ZLinkRelayMetadataPolicy metadataPolicy) {
        this.sourceEntrySpot = sourceEntrySpot;
        this.routedTransport = routedTransport;
        this.routeChannelName = routeChannelName;
        this.targetNodeRid = targetNodeRid;
        this.targetEntrySpotId = targetEntrySpotId;
        this.actorRef = actorRef;
        this.serializer = serializer;
        this.actorRuntime = actorRuntime;
        this.actor = actor;
        this.timeout = timeout;
        this.defaultCodec = defaultCodec == null ? ZLinkStreamCodec.JSON : defaultCodec;
        this.metadataPolicy =
            metadataPolicy == null ? ZLinkRelayMetadataPolicy.EMPTY : metadataPolicy;
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
            targetEntrySpotId,
            actorRef,
            encoded.payload(),
            timeout,
            ZLinkBoundSessionSendOptions.create(encoded.packetName(), defaultCodec),
            metadataPolicy);
    }

    CompletionStage<Void> sendFrame(byte[] frameBytes) {
        try (Message frame = Message.from(frameBytes)) {
            return sendFrame(
                sourceEntrySpot,
                routedTransport,
                routeChannelName,
                targetNodeRid,
                targetEntrySpotId,
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
        String targetEntrySpotId,
        ZLinkBackendActorRef actorRef,
        Message frame) {
        List<Message> parts = ZLinkActorSpotRoutePackets.createBoundSessionSendParts(actorRef, frame);
        try {
            if (routedTransport != null && routeChannelName != null && !routeChannelName.isBlank()) {
                return routedTransport.sendToSpotViaRouterChannel(
                    routeChannelName,
                    targetNodeRid,
                    targetEntrySpotId,
                    parts);
            }
            boolean submitted = sourceEntrySpot.sendToSpot(
                    targetNodeRid,
                    targetEntrySpotId,
                    0L,
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
        String targetEntrySpotId,
        ZLinkBackendActorRef actorRef,
        Message payload,
        Duration timeout,
        ZLinkBoundSessionSendOptions options,
        ZLinkRelayMetadataPolicy metadataPolicy,
        systems.zlink.framework.runtime.messaging.ZLinkOneWayCallGate submitGate)
        implements ZLinkBoundSessionSendCall {
        SendCall(
            ZLinkBackendSpot sourceEntrySpot,
            ZLinkChannelRuntime routedTransport,
            String routeChannelName,
            RoutingId targetNodeRid,
            String targetEntrySpotId,
            ZLinkBackendActorRef actorRef,
            Message payload,
            Duration timeout,
            ZLinkBoundSessionSendOptions options,
            ZLinkRelayMetadataPolicy metadataPolicy) {
            this(sourceEntrySpot, routedTransport, routeChannelName, targetNodeRid,
                targetEntrySpotId, actorRef, payload, timeout, options, metadataPolicy,
                new systems.zlink.framework.runtime.messaging.ZLinkOneWayCallGate());
        }
        public ZLinkBoundSessionSendCall packetName(String packetName) {
            return new SendCall(
                sourceEntrySpot,
                routedTransport,
                routeChannelName,
                targetNodeRid,
                targetEntrySpotId,
                actorRef,
                payload,
                timeout,
                options.withPacketName(packetName),
                metadataPolicy);
        }

        @Override
        public ZLinkBoundSessionSendCall metadata(String key, String value) {
            return new SendCall(
                sourceEntrySpot,
                routedTransport,
                routeChannelName,
                targetNodeRid,
                targetEntrySpotId,
                actorRef,
                payload,
                timeout,
                options.withMetadata(key, value),
                metadataPolicy);
        }

        @Override
        public CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> submit() {
            CompletionStage<systems.zlink.framework.channels.ZLinkSubmitResult> duplicate =
                submitGate.begin();
            if (duplicate != null) {
                return duplicate;
            }
            byte[] frameBytes;
            try {
                frameBytes = metadataPolicy.actorToSession(options).encodeFrame(payload);
            } finally {
                payload.close();
            }
            Message frame = Message.from(frameBytes);
            try {
                return ZLinkSubmitResults.fromVoidStage(
                    ZLinkRoutedBoundSessionRuntime.sendFrame(
                        sourceEntrySpot,
                        routedTransport,
                        routeChannelName,
                        targetNodeRid,
                        targetEntrySpotId,
                        actorRef,
                        frame).whenComplete((ignored, error) -> frame.close()));
            } catch (RuntimeException error) {
                frame.close();
                throw error;
            }
        }

    }

}
