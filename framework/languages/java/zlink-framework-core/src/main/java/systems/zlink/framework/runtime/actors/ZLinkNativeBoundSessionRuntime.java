package systems.zlink.framework.runtime.actors;

import systems.zlink.framework.runtime.backend.*;

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
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkBoundSession;
import systems.zlink.framework.actors.ZLinkBoundSessionSendCall;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.execution.ZLinkFrameworkTurns;
import systems.zlink.framework.execution.ZLinkYieldTurn;
import systems.zlink.framework.runtime.messaging.ZLinkPayloadEncoding;
import systems.zlink.framework.runtime.streams.ZLinkStreamFrameCodec;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderFlag;
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
        ZLinkBackendSpotNode spotNode,
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
            encoded.packetName(),
            Map.of(),
            Optional.empty(),
            timeout,
            defaultCodec,
            ZLinkFrameworkTurns.captureCurrent());
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
        ZLinkBackendSpotNode spotNode,
        ZLinkActorRuntime actorRuntime,
        ZLinkActor actor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
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
                spotNode,
                actorRuntime,
                actor,
                sourceNodeRid,
                sourceSessionRid,
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
                spotNode,
                actorRuntime,
                actor,
                sourceNodeRid,
                sourceSessionRid,
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
            ZLinkBackendActorRef currentActorRef = actorRuntime.actorRef(actor);
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
        public CompletionStage<Void> yieldAsync() {
            return ZLinkFrameworkTurns.awaitManagedCompletion(requireTurn(), submit());
        }

        private ZLinkYieldTurn requireTurn() {
            if (turn == null) {
                throw new IllegalStateException(
                    "yieldAwait requires a framework Spot handler turn captured when the call object was created");
            }
            return turn;
        }
    }

    private static CompletionStage<Void> sendWithRetry(
        ZLinkBackendSpotNode spotNode,
        ZLinkBackendActorRef actorRef,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
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
                    boolean sent = spotNode.sendActorBoundSession(
                        actorRef,
                        List.of(frame),
                        SendFlags.DONT_WAIT);
                    if (sent) {
                        result.complete(null);
                        return;
                    }
                } catch (ZlinkSubmitException ex) {
                    if (!isRetryableSubmitResult(ex.getResult())) {
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
        RETRY_EXECUTOR.execute(new Attempt());
        return result;
    }

    private static boolean isRetryableSubmitResult(SubmitResult result) {
        return result == SubmitResult.NOT_CONNECTED
            || result == SubmitResult.BACKPRESSURED
            || result == SubmitResult.NOT_FOUND;
    }

}
