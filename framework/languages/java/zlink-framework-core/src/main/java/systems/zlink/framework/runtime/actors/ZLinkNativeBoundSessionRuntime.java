package systems.zlink.framework.runtime.actors;

import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;

import systems.zlink.framework.runtime.backend.*;

import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkBoundSession;
import systems.zlink.framework.actors.ZLinkBoundSessionSendCall;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.streams.ZLinkStreamCodec;

final class ZLinkNativeBoundSessionRuntime implements ZLinkBoundSession {
    private final ZLinkInternalSpotNode spotNode;
    private ZLinkBackendActorRef actorRef;
    private final ZLinkMessageSerializer serializer;
    private final ZLinkActorRuntime actorRuntime;
    private final ZLinkActor actor;
    private final RoutingId sourceNodeRid;
    private final RoutingId sourceSessionRid;
    private final Duration timeout;
    private final ZLinkStreamCodec defaultCodec;
    private long bindingToken;

    ZLinkNativeBoundSessionRuntime(
        ZLinkInternalSpotNode spotNode,
        ZLinkBackendActorRef actorRef,
        ZLinkMessageSerializer serializer,
        ZLinkActorRuntime actorRuntime,
        ZLinkActor actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        Duration timeout,
        ZLinkStreamCodec defaultCodec) {
        this.spotNode = spotNode;
        this.actorRef = actorRef;
        this.serializer = serializer;
        this.actorRuntime = actorRuntime;
        this.actor = actor;
        this.sourceNodeRid = sourceNodeRid;
        this.sourceSessionRid = sourceSessionRid;
        this.timeout = timeout;
        this.defaultCodec = defaultCodec == null ? ZLinkStreamCodec.JSON : defaultCodec;
    }

    void setBindingToken(long bindingToken) {
        this.bindingToken = bindingToken;
    }

    void updateActorRef(ZLinkBackendActorRef actorRef) {
        this.actorRef = actorRef;
    }

    @Override
    public ZLinkBoundSessionSendCall send(Object message) {
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, message);
        return new SendCall(
            spotNode,
            actorRuntime,
            actor,
            sourceNodeRid,
            sourceSessionRid,
            encoded.payload(),
            timeout,
            ZLinkBoundSessionSendOptions.create(encoded.packetName(), defaultCodec));
    }

    CompletionStage<Void> sendFrame(byte[] frameBytes) {
        ZLinkBackendActorRef currentActorRef = currentActorRef();
        return sendWithRetry(
            spotNode,
            currentActorRef,
            sourceNodeRid,
            sourceSessionRid,
            frameBytes,
            timeout,
            "actor bound session send failed: " + currentActorRef.actorId());
    }

    @Override
    public CompletionStage<Void> disconnect() {
        return CompletableFuture.runAsync(() -> spotNode.closeActorBoundSession(currentActorRef(), timeout))
            .thenRun(() -> actorRuntime.clearSessionBinding(actor, bindingToken));
    }

    private ZLinkBackendActorRef currentActorRef() {
        return actorRuntime.actorRef(actor);
    }

    private record SendCall(
        ZLinkInternalSpotNode spotNode,
        ZLinkActorRuntime actorRuntime,
        ZLinkActor actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        Message payload,
        Duration timeout,
        ZLinkBoundSessionSendOptions options) implements ZLinkBoundSessionSendCall {
        public ZLinkBoundSessionSendCall packetName(String packetName) {
            return new SendCall(
                spotNode,
                actorRuntime,
                actor,
                sourceNodeRid,
                sourceSessionRid,
                payload,
                timeout,
                options.withPacketName(packetName));
        }

        @Override
        public ZLinkBoundSessionSendCall metadata(String key, String value) {
            return new SendCall(
                spotNode,
                actorRuntime,
                actor,
                sourceNodeRid,
                sourceSessionRid,
                payload,
                timeout,
                options.withMetadata(key, value));
        }

        @Override
        public void submit() {
            ZLinkBackendActorRef currentActorRef = actorRuntime.actorRef(actor);
            byte[] frameBytes;
            try {
                frameBytes = options.encodeFrame(payload);
            } finally {
                payload.close();
            }
            sendWithRetry(
                spotNode,
                currentActorRef,
                sourceNodeRid,
                sourceSessionRid,
                frameBytes,
                timeout,
                "actor bound session send failed: " + currentActorRef.actorId())
                .exceptionally(error -> {
                    java.util.logging.Logger.getLogger(ZLinkNativeBoundSessionRuntime.class.getName())
                        .log(java.util.logging.Level.SEVERE, "native bound-session send failed", error);
                    return null;
                });
        }

    }

    private static CompletionStage<Void> sendWithRetry(
        ZLinkInternalSpotNode spotNode,
        ZLinkBackendActorRef actorRef,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        byte[] frameBytes,
        Duration timeout,
        String failureMessage) {
        return ZLinkActorRetryScheduler.submitNativeBoundSessionUntilAcceptedAsync(
            timeout,
            () -> {
                try (Message frame = Message.from(frameBytes)) {
                    return spotNode.sendActorBoundSession(
                        actorRef,
                        List.of(frame),
                        SendFlags.DONT_WAIT);
                }
            },
            () -> new ZLinkConfigurationException(failureMessage));
    }

}
