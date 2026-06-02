package systems.zlink.framework.runtime.actors;

import systems.zlink.framework.runtime.backend.*;

import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkBoundSession;
import systems.zlink.framework.actors.ZLinkBoundSessionSendCall;

final class ZLinkBoundSessionRuntime implements ZLinkBoundSession {
    private static final java.time.Duration DEFAULT_TIMEOUT = java.time.Duration.ofSeconds(30);
    private final ZLinkBackendStreamSocket stream;
    private final RoutingId sessionRid;
    private final String actorId;
    private final ZLinkMessageSerializer serializer;
    private final ZLinkActorRuntime actorRuntime;
    private final ZLinkActor actor;
    private long bindingToken;

    ZLinkBoundSessionRuntime(
        ZLinkBackendStreamSocket stream,
        RoutingId sessionRid,
        String actorId,
        ZLinkMessageSerializer serializer,
        ZLinkActorRuntime actorRuntime,
        ZLinkActor actor) {
        this.stream = stream;
        this.sessionRid = sessionRid;
        this.actorId = actorId;
        this.serializer = serializer;
        this.actorRuntime = actorRuntime;
        this.actor = actor;
    }

    void setBindingToken(long bindingToken) {
        this.bindingToken = bindingToken;
    }

    @Override
    public <TMessage> ZLinkBoundSessionSendCall send(TMessage message) {
        return new SendCall(
            stream,
            sessionRid,
            actorId,
            serializer.serialize(message),
            Optional.empty());
    }

    @Override
    public CompletionStage<Void> disconnectAsync() {
        return stream.unbindActor(sessionRid, actorId)
            .submitAsync(DEFAULT_TIMEOUT)
            .thenRun(() -> actorRuntime.clearSessionBinding(actor, bindingToken));
    }

    private record SendCall(
        ZLinkBackendStreamSocket stream,
        RoutingId sessionRid,
        String actorId,
        Message payload,
        Optional<String> packetName) implements ZLinkBoundSessionSendCall {
        @Override
        public ZLinkBoundSessionSendCall packetName(String packetName) {
            if (packetName == null || packetName.isBlank()) {
                throw new IllegalArgumentException("packetName is required");
            }
            return new SendCall(stream, sessionRid, actorId, payload, Optional.of(packetName));
        }

        @Override
        public ZLinkBoundSessionSendCall metadata(String key, String value) {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            try {
                stream.sendBoundActor(
                    sessionRid,
                    actorId,
                    parts(packetName, payload),
                    SendFlags.NONE);
                return CompletableFuture.completedFuture(null);
            } finally {
                payload.close();
            }
        }

        private static List<Message> parts(Optional<String> packetName, Message payload) {
            if (packetName.isEmpty()) {
                return List.of(payload);
            }
            return List.of(
                Message.from(packetName.get().getBytes(StandardCharsets.UTF_8)),
                payload);
        }
    }
}
