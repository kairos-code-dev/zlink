package systems.zlink.framework.runtime.actors;

import systems.zlink.framework.runtime.backend.*;

import java.util.EnumSet;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ScheduledThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import systems.zlink.contracts.errors.ConfigResult;
import systems.zlink.contracts.errors.ZlinkConfigException;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.framework.ZLinkAwait;
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

final class ZLinkBoundSessionRuntime implements ZLinkBoundSession {
    private static final java.time.Duration DEFAULT_TIMEOUT = java.time.Duration.ofSeconds(30);
    private static final ScheduledThreadPoolExecutor RETRY_EXECUTOR =
        new ScheduledThreadPoolExecutor(1, task -> {
            Thread thread = new Thread(task, "zlink-bound-session-retry");
            thread.setDaemon(true);
            return thread;
        });
    private static final String REMOTE_BOUND_SESSION_BIND_PACKET_NAME =
        "zlink.framework.actor.bound_session.bind";

    static {
        RETRY_EXECUTOR.setRemoveOnCancelPolicy(true);
        RETRY_EXECUTOR.prestartCoreThread();
    }
    private final ZLinkBackendStreamSocket stream;
    private final ZLinkBackendSpotNode spotNode;
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
        ZLinkBackendSpotNode spotNode,
        RoutingId sessionRid,
        String actorId,
        ZLinkMessageSerializer serializer,
        ZLinkActorRuntime actorRuntime,
        ZLinkActor actor,
        ZLinkStreamCodec defaultCodec) {
        this.stream = stream;
        this.spotNode = spotNode;
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

    CompletionStage<Void> rebindNativeActor(
        ZLinkBackendActorRef targetActor,
        java.time.Duration timeout) {
        if (!actorId.equals(targetActor.actorId())) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "bound session actor id mismatch: " + actorId));
        }
        ZLinkStreamHeader header = new ZLinkStreamHeader(
            ZLinkStreamMessageKind.SEND,
            ZLinkStreamCodec.RAW,
            EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
            Optional.empty(),
            REMOTE_BOUND_SESSION_BIND_PACKET_NAME,
            Map.of());
        return ignoreMissingBinding(stream.unbindActor(sessionRid, actorId).submit(timeout))
            .thenCompose(unbound -> bindActorWithRetry(stream, sessionRid, targetActor, timeout)
            .thenCompose(ignored -> {
                try (Message body = Message.from(new byte[0])) {
                    if (stream.relayBoundActor(
                        sessionRid,
                        actorId,
                        header,
                        List.of(body),
                        SendFlags.NONE)) {
                        return CompletableFuture.completedFuture(null);
                    }
                    return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                        "remote bound session bind relay failed: " + actorId));
                }
            }));
    }

    private static CompletionStage<Void> bindActorWithRetry(
        ZLinkBackendStreamSocket stream,
        RoutingId sessionRid,
        ZLinkBackendActorRef targetActor,
        java.time.Duration timeout) {
        CompletableFuture<Void> result = new CompletableFuture<>();
        long deadline = System.nanoTime() + timeout.toNanos();
        class Attempt implements Runnable {
            @Override
            public void run() {
                if (result.isDone()) {
                    return;
                }
                stream.bindActor(sessionRid, targetActor)
                    .submit(java.time.Duration.ofSeconds(2))
                    .whenComplete((ignored, error) -> {
                        if (error == null || isAlreadyBound(error)) {
                            result.complete(null);
                            return;
                        }
                        if (!isRetriableBindFailure(error) || System.nanoTime() >= deadline) {
                            result.completeExceptionally(error);
                            return;
                        }
                        RETRY_EXECUTOR.schedule(this, 10, TimeUnit.MILLISECONDS);
                    });
            }
        }
        new Attempt().run();
        return result;
    }

    private static CompletionStage<Void> ignoreMissingBinding(CompletionStage<Void> stage) {
        return stage.handle((ignored, error) -> {
            if (error == null || isNotFound(error)) {
                return CompletableFuture.<Void>completedFuture(null);
            }
            return CompletableFuture.<Void>failedFuture(error);
        }).thenCompose(result -> result);
    }

    private static boolean isNotFound(Throwable error) {
        Throwable current = error;
        while (current != null) {
            if (current instanceof ZlinkRequestException request) {
                return request.getResult() == RequestResult.NOT_FOUND;
            }
            current = current.getCause();
        }
        return false;
    }

    private static boolean isAlreadyBound(Throwable error) {
        ZlinkRequestException request = findRequestException(error);
        return request != null
            && (request.getResult() == RequestResult.CONFLICT
                || request.getResult() == RequestResult.BUSY
                || request.getNativeErrno() == 16);
    }

    private static boolean isRetriableBindFailure(Throwable error) {
        ZlinkRequestException request = findRequestException(error);
        if (request != null
            && (request.getResult() == RequestResult.NOT_FOUND
                || request.getResult() == RequestResult.NOT_CONNECTED
                || request.getResult() == RequestResult.BUSY
                || request.getNativeErrno() == 11
                || request.getNativeErrno() == 16)) {
            return true;
        }
        ZlinkConfigException config = findConfigException(error);
        return config != null && config.getResult() == ConfigResult.NOT_FOUND;
    }

    private static ZlinkRequestException findRequestException(Throwable error) {
        Throwable current = error;
        while (current != null) {
            if (current instanceof ZlinkRequestException request) {
                return request;
            }
            current = current.getCause();
        }
        return null;
    }

    private static ZlinkConfigException findConfigException(Throwable error) {
        Throwable current = error;
        while (current != null) {
            if (current instanceof ZlinkConfigException config) {
                return config;
            }
            current = current.getCause();
        }
        return null;
    }

    @Override
    public ZLinkBoundSessionSendCall send(Object message) {
        ZLinkPayloadEncoding.EncodedPayload encoded =
            ZLinkPayloadEncoding.encode(serializer, message);
        return new SendCall(
            stream,
            sessionRid,
            actorId,
            encoded.payload(),
            encoded.packetName(),
            Map.of(),
            Optional.empty(),
            defaultCodec,
            ZLinkFrameworkTurns.captureCurrent());
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
        ZLinkStreamCodec codec,
        ZLinkYieldTurn turn) implements ZLinkBoundSessionSendCall {
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
                codec,
                turn);
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
                codec,
                turn);
        }

        @Override
        public CompletionStage<Void> submit() {
            byte[] payloadBytes;
            try {
                payloadBytes = payload.toByteArray();
            } finally {
                payload.close();
            }
            ZLinkStreamHeader header = new ZLinkStreamHeader(
                ZLinkStreamMessageKind.SEND,
                codec,
                EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
                Optional.empty(),
                packetName.orElse(defaultPacketName),
                metadata);
            return sendWithRetry(stream, sessionRid, header, payloadBytes, actorId);
        }

        private ZLinkYieldTurn requireTurn() {
            if (turn == null) {
                ZLinkYieldTurn current = ZLinkFrameworkTurns.captureCurrent();
                if (current != null) {
                    return current;
                }
                throw new IllegalStateException(
                    "yield requires a framework Spot handler turn captured when the call object was created");
            }
            return turn;
        }
    }

    private static CompletionStage<Void> sendWithRetry(
        ZLinkBackendStreamSocket stream,
        RoutingId sessionRid,
        ZLinkStreamHeader header,
        byte[] payloadBytes,
        String actorId) {
        CompletableFuture<Void> result = new CompletableFuture<>();
        long deadline = System.nanoTime() + DEFAULT_TIMEOUT.toNanos();
        class Attempt implements Runnable {
            @Override
            public void run() {
                if (result.isDone()) {
                    return;
                }
                try (Message payloadPart = Message.from(payloadBytes)) {
                    if (stream.relayBoundActor(
                        sessionRid,
                        actorId,
                        header,
                        List.of(payloadPart),
                        SendFlags.DONT_WAIT)) {
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
                    result.completeExceptionally(new ZLinkConfigurationException(
                        "bound session send failed: " + actorId));
                    return;
                }
                RETRY_EXECUTOR.schedule(this, 10, TimeUnit.MILLISECONDS);
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
