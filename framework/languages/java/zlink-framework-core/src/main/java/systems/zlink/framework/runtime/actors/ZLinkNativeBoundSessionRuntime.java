package systems.zlink.framework.runtime.actors;

import systems.zlink.framework.runtime.backend.*;

import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.List;
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
    private long bindingToken;

    ZLinkNativeBoundSessionRuntime(
        ZLinkBackendSpotNode spotNode,
        ZLinkBackendActorRef actorRef,
        ZLinkMessageSerializer serializer,
        ZLinkActorRuntime actorRuntime,
        ZLinkActor actor,
        Duration timeout) {
        this.spotNode = spotNode;
        this.actorRef = actorRef;
        this.serializer = serializer;
        this.actorRuntime = actorRuntime;
        this.actor = actor;
        this.timeout = timeout;
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
            Optional.empty(),
            timeout);
    }

    @Override
    public CompletionStage<Void> disconnectAsync() {
        return CompletableFuture.runAsync(() -> spotNode.closeActorBoundSession(actorRef, timeout))
            .thenRun(() -> actorRuntime.clearSessionBinding(actor, bindingToken));
    }

    private record SendCall(
        ZLinkBackendSpotNode spotNode,
        ZLinkBackendActorRef actorRef,
        Message payload,
        String defaultPacketName,
        Optional<String> packetName,
        Duration timeout) implements ZLinkBoundSessionSendCall {
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
                Optional.of(packetName),
                timeout);
        }

        @Override
        public ZLinkBoundSessionSendCall metadata(String key, String value) {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            byte[] frameBytes;
            try {
                frameBytes = encodeStreamFrame(
                    1,
                    0,
                    packetName.orElse(defaultPacketName),
                    payload.toByteArray());
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

    private static byte[] encodeStreamFrame(
        int kind,
        int codec,
        String packetName,
        byte[] body) {
        byte[] name = packetName.getBytes(StandardCharsets.UTF_8);
        ByteBuffer header = ByteBuffer.allocate(4 + name.length);
        header.put((byte) kind);
        header.put((byte) codec);
        header.put((byte) 0);
        header.put((byte) name.length);
        header.put(name);
        byte[] headerBytes = header.array();
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
