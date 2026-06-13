package systems.zlink.framework.runtime.actors;

import systems.zlink.framework.runtime.backend.*;

import java.util.EnumSet;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
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
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.handlers.ZLinkPacket;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.framework.streams.ZLinkStreamHeaderFlag;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;

final class ZLinkBoundSessionRuntime implements ZLinkBoundSession {
    private static final java.time.Duration DEFAULT_TIMEOUT = java.time.Duration.ofSeconds(30);
    private final ZLinkBackendStreamSocket stream;
    private final RoutingId sessionRid;
    private final String actorId;
    private final ZLinkMessageSerializer serializer;
    private final ZLinkActorRuntime actorRuntime;
    private final ZLinkActor actor;
    private final ZLinkStreamCodec defaultCodec;
    private long bindingToken;
    private Runnable unbindListener = () -> {};

    ZLinkBoundSessionRuntime(
        ZLinkBackendStreamSocket stream,
        RoutingId sessionRid,
        String actorId,
        ZLinkMessageSerializer serializer,
        ZLinkActorRuntime actorRuntime,
        ZLinkActor actor,
        ZLinkStreamCodec defaultCodec) {
        this.stream = stream;
        this.sessionRid = sessionRid;
        this.actorId = actorId;
        this.serializer = serializer;
        this.actorRuntime = actorRuntime;
        this.actor = actor;
        this.defaultCodec = defaultCodec == null ? ZLinkStreamCodec.JSON : defaultCodec;
    }

    void setBindingToken(long bindingToken) {
        this.bindingToken = bindingToken;
    }

    void setUnbindListener(Runnable unbindListener) {
        this.unbindListener = unbindListener == null ? () -> {} : unbindListener;
    }

    @Override
    public <TMessage> ZLinkBoundSessionSendCall send(TMessage message) {
        return new SendCall(
            stream,
            sessionRid,
            actorId,
            serializer.serialize(message),
            packetNameFor(message),
            Map.of(),
            Optional.empty(),
            defaultCodec);
    }

    @Override
    public CompletionStage<Void> disconnect() {
        return stream.unbindActor(sessionRid, actorId)
            .submit(DEFAULT_TIMEOUT)
            .thenRun(() -> {
                actorRuntime.clearSessionBinding(actor, bindingToken);
                unbindListener.run();
            });
    }

    private record SendCall(
        ZLinkBackendStreamSocket stream,
        RoutingId sessionRid,
        String actorId,
        Message payload,
        String defaultPacketName,
        Map<String, String> metadata,
        Optional<String> packetName,
        ZLinkStreamCodec codec) implements ZLinkBoundSessionSendCall {
        @Override
        public ZLinkBoundSessionSendCall packetName(String packetName) {
            if (packetName == null || packetName.isBlank()) {
                throw new IllegalArgumentException("packetName is required");
            }
            return new SendCall(
                stream,
                sessionRid,
                actorId,
                payload,
                defaultPacketName,
                metadata,
                Optional.of(packetName),
                codec);
        }

        @Override
        public ZLinkBoundSessionSendCall metadata(String key, String value) {
            Map<String, String> next = new HashMap<>(metadata);
            next.put(key, value);
            return new SendCall(
                stream,
                sessionRid,
                actorId,
                payload,
                defaultPacketName,
                Map.copyOf(next),
                packetName,
                codec);
        }

        @Override
        public CompletionStage<Void> submit() {
            try {
                ZLinkStreamHeader header = new ZLinkStreamHeader(
                    ZLinkStreamMessageKind.SEND,
                    codec,
                    EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
                    Optional.empty(),
                    packetName.orElse(defaultPacketName),
                    metadata);
                if (!stream.send(
                    sessionRid,
                    header,
                    List.of(payload),
                    SendFlags.DONT_WAIT)) {
                    return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                        "bound session send failed: " + actorId));
                }
                return CompletableFuture.completedFuture(null);
            } finally {
                payload.close();
            }
        }
    }

    private static String packetNameFor(Object message) {
        if (message == null) {
            return "Null";
        }
        Class<?> messageType = message.getClass();
        ZLinkPacket packet = messageType.getAnnotation(ZLinkPacket.class);
        return packet == null ? messageType.getSimpleName() : packet.value();
    }
}
