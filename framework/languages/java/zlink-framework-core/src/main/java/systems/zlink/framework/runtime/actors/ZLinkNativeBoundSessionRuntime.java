package systems.zlink.framework.runtime.actors;

import systems.zlink.framework.runtime.backend.*;

import java.nio.ByteBuffer;
import java.time.Duration;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ScheduledThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkBoundSession;
import systems.zlink.framework.actors.ZLinkBoundSessionSendCall;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.handlers.ZLinkPacket;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodec;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.streams.ZLinkStreamHeader;
import systems.zlink.framework.streams.ZLinkStreamHeaderFlag;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;

final class ZLinkNativeBoundSessionRuntime implements ZLinkBoundSession {
    private static final ScheduledThreadPoolExecutor RETRY_EXECUTOR =
        new ScheduledThreadPoolExecutor(1, task -> {
            Thread thread = new Thread(task, "zlink-native-bound-session-retry");
            thread.setDaemon(true);
            return thread;
        });

    static {
        RETRY_EXECUTOR.setRemoveOnCancelPolicy(true);
        RETRY_EXECUTOR.prestartCoreThread();
    }

    private final ZLinkBackendSpotNode spotNode;
    private final ZLinkBackendActorRef actorRef;
    private final ZLinkMessageSerializer serializer;
    private final ZLinkActorRuntime actorRuntime;
    private final ZLinkActor actor;
    private final Duration timeout;
    private final ZLinkStreamCodec defaultCodec;
    private long bindingToken;

    ZLinkNativeBoundSessionRuntime(
        ZLinkBackendSpotNode spotNode,
        ZLinkBackendActorRef actorRef,
        ZLinkMessageSerializer serializer,
        ZLinkActorRuntime actorRuntime,
        ZLinkActor actor,
        Duration timeout,
        ZLinkStreamCodec defaultCodec) {
        this.spotNode = spotNode;
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
    public <TMessage> ZLinkBoundSessionSendCall send(TMessage message) {
        return new SendCall(
            spotNode,
            actorRef,
            serializer.serialize(message),
            packetNameFor(message),
            Map.of(),
            Optional.empty(),
            timeout,
            defaultCodec);
    }

    @Override
    public CompletionStage<Void> disconnect() {
        return CompletableFuture.runAsync(() -> spotNode.closeActorBoundSession(actorRef, timeout))
            .thenRun(() -> actorRuntime.clearSessionBinding(actor, bindingToken));
    }

    private record SendCall(
        ZLinkBackendSpotNode spotNode,
        ZLinkBackendActorRef actorRef,
        Message payload,
        String defaultPacketName,
        Map<String, String> metadata,
        Optional<String> packetName,
        Duration timeout,
        ZLinkStreamCodec codec) implements ZLinkBoundSessionSendCall {
        @Override
        public ZLinkBoundSessionSendCall packetName(String packetName) {
            if (packetName == null || packetName.isBlank()) {
                throw new IllegalArgumentException("packetName is required");
            }
            return new SendCall(
                spotNode,
                actorRef,
                payload,
                defaultPacketName,
                metadata,
                Optional.of(packetName),
                timeout,
                codec);
        }

        @Override
        public ZLinkBoundSessionSendCall metadata(String key, String value) {
            Map<String, String> next = new HashMap<>(metadata);
            next.put(key, value);
            return new SendCall(
                spotNode,
                actorRef,
                payload,
                defaultPacketName,
                Map.copyOf(next),
                packetName,
                timeout,
                codec);
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
                frameBytes = encodeStreamFrame(header, payload.toByteArray());
            } finally {
                payload.close();
            }
            return sendWithRetry(
                spotNode,
                actorRef,
                frameBytes,
                timeout,
                "actor bound session send failed: " + actorRef.actorId());
        }
    }

    private static CompletionStage<Void> sendWithRetry(
        ZLinkBackendSpotNode spotNode,
        ZLinkBackendActorRef actorRef,
        byte[] frameBytes,
        Duration timeout,
        String failureMessage) {
        CompletableFuture<Void> result = new CompletableFuture<>();
        long deadline = System.nanoTime() + timeout.toNanos();
        class Attempt implements Runnable {
            @Override
            public void run() {
                if (result.isDone()) {
                    return;
                }
                try (Message frame = Message.from(frameBytes)) {
                    if (spotNode.sendActorBoundSession(actorRef, List.of(frame), SendFlags.DONT_WAIT)) {
                        result.complete(null);
                        return;
                    }
                } catch (ZlinkSubmitException ex) {
                    if (ex.getResult() != SubmitResult.NOT_CONNECTED) {
                        result.completeExceptionally(ex);
                        return;
                    }
                } catch (RuntimeException ex) {
                    result.completeExceptionally(ex);
                    return;
                }
                if (System.nanoTime() >= deadline) {
                    result.completeExceptionally(new ZLinkConfigurationException(failureMessage));
                    return;
                }
                RETRY_EXECUTOR.schedule(this, 25, TimeUnit.MILLISECONDS);
            }
        }
        new Attempt().run();
        return result;
    }

    private static byte[] encodeStreamFrame(ZLinkStreamHeader header, byte[] body) {
        byte[] headerBytes = ZLinkStreamHeaderCodec.encode(header);
        ByteBuffer frame = ByteBuffer.allocate(6 + headerBytes.length + body.length);
        frame.putShort((short) headerBytes.length);
        frame.putInt(body.length);
        frame.put(headerBytes);
        frame.put(body);
        return frame.array();
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
