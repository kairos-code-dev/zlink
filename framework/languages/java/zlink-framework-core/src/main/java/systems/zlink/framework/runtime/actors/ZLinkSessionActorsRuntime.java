package systems.zlink.framework.runtime.actors;

import systems.zlink.framework.runtime.backend.*;

import java.time.Duration;
import java.util.ArrayList;
import java.util.EnumSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.function.Predicate;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.runtime.messaging.ZLinkMessagePayloads;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionActors;
import systems.zlink.framework.streams.ZLinkStreamCodec;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderFlag;
import systems.zlink.framework.streams.ZLinkStreamMessageKind;

public final class ZLinkSessionActorsRuntime implements ZLinkSessionActors {
    private static final Duration RELAY_SUBMIT_TIMEOUT = Duration.ofSeconds(30);
    private static final ThreadLocal<ZLinkStreamHeader> CURRENT_RELAY_HEADER = new ThreadLocal<>();
    private static final ScheduledExecutorService RELAY_RETRY_EXECUTOR =
        Executors.newSingleThreadScheduledExecutor(task -> {
            Thread thread = new Thread(task, "zlink-java-session-actor-relay");
            thread.setDaemon(true);
            return thread;
        });

    private final ZLinkBackendStreamSocket stream;
    private final ZLinkBackendSpotNode spotNode;
    private final RoutingId sessionRid;
    private final ZLinkActorRuntime actors;
    private final ZLinkMessageSerializer serializer;
    private final Predicate<RoutingId> routeReady;
    private final LocalActorDispatcher localActorDispatcher;
    private final boolean nativeSessionRelayAttached;
    private final ZLinkStreamCodec defaultCodec;
    private final List<ZLinkSessionActor> bound = new ArrayList<>();

    @FunctionalInterface
    public interface LocalActorDispatcher {
        CompletionStage<Optional<Message>> dispatch(
            ZLinkBackendActorRef actor,
            ZLinkStreamHeader header,
            Message payload);
    }

    public static void enterRelayDispatch(ZLinkStreamHeader header) {
        CURRENT_RELAY_HEADER.set(header);
    }

    public static void exitRelayDispatch() {
        CURRENT_RELAY_HEADER.remove();
    }

    private static Optional<ZLinkStreamHeader> currentRelayHeader() {
        return Optional.ofNullable(CURRENT_RELAY_HEADER.get());
    }

    public ZLinkSessionActorsRuntime(
        ZLinkBackendStreamSocket stream,
        RoutingId sessionRid,
        ZLinkActorRuntime actors,
        ZLinkMessageSerializer serializer) {
        this(
            stream,
            sessionRid,
            actors,
            serializer,
            ignored -> true,
            null,
            true,
            ZLinkStreamCodec.JSON);
    }

    public ZLinkSessionActorsRuntime(
        ZLinkBackendStreamSocket stream,
        RoutingId sessionRid,
        ZLinkActorRuntime actors,
        ZLinkMessageSerializer serializer,
        Predicate<RoutingId> routeReady) {
        this(
            stream,
            sessionRid,
            actors,
            serializer,
            routeReady,
            null,
            true,
            ZLinkStreamCodec.JSON);
    }

    public ZLinkSessionActorsRuntime(
        ZLinkBackendStreamSocket stream,
        RoutingId sessionRid,
        ZLinkActorRuntime actors,
        ZLinkMessageSerializer serializer,
        Predicate<RoutingId> routeReady,
        LocalActorDispatcher localActorDispatcher,
        boolean nativeSessionRelayAttached,
        ZLinkStreamCodec defaultCodec) {
        this(null, stream, sessionRid, actors, serializer, routeReady,
            localActorDispatcher, nativeSessionRelayAttached, defaultCodec);
    }

    public ZLinkSessionActorsRuntime(
        ZLinkBackendSpotNode spotNode,
        ZLinkBackendStreamSocket stream,
        RoutingId sessionRid,
        ZLinkActorRuntime actors,
        ZLinkMessageSerializer serializer,
        Predicate<RoutingId> routeReady,
        LocalActorDispatcher localActorDispatcher,
        boolean nativeSessionRelayAttached,
        ZLinkStreamCodec defaultCodec) {
        this.spotNode = spotNode;
        this.stream = stream;
        this.sessionRid = sessionRid;
        this.actors = actors;
        this.serializer = serializer;
        this.routeReady = routeReady == null ? ignored -> true : routeReady;
        this.localActorDispatcher = localActorDispatcher;
        this.nativeSessionRelayAttached = nativeSessionRelayAttached;
        this.defaultCodec = defaultCodec == null ? ZLinkStreamCodec.JSON : defaultCodec;
    }

    @Override
    public List<ZLinkSessionActor> bound() {
        return List.copyOf(bound);
    }

    @Override
    public CompletionStage<ZLinkSessionActor> bind(ZLinkActor actor) {
        return bindManagedAsync(actor);
    }

    @Override
    public CompletionStage<ZLinkSessionActor> bind(ZLinkActorRef actor) {
        ZLinkBackendActorRef ref = new ZLinkBackendActorRef(
            actor.nodeRid(),
            actor.actorId(),
            actor.generation());
        return bindBackendRef(ref);
    }

    @Override
    public CompletionStage<ZLinkSessionActor> bindOrGet(ZLinkActorRef actor) {
        ZLinkBackendActorRef ref = new ZLinkBackendActorRef(
            actor.nodeRid(),
            actor.actorId(),
            actor.generation());
        Optional<ZLinkSessionActor> existing = bound.stream()
            .filter(boundActor -> sameRef(boundActor.ref(), actor))
            .findFirst();
        return existing
            .<CompletionStage<ZLinkSessionActor>>map(CompletableFuture::completedFuture)
            .orElseGet(() -> bindBackendRef(ref));
    }

    @Override
    public Optional<ZLinkSessionActor> find(String actorId) {
        return bound.stream()
            .filter(actor -> actor.actorId().equals(actorId))
            .findFirst();
    }

    public CompletionStage<Void> notifyDisconnectedAll() {
        List<ZLinkSessionActor> current = List.copyOf(bound);
        return CompletableFuture.allOf(current.stream()
            .map(actor -> actor.notifyDisconnected().toCompletableFuture())
            .toArray(CompletableFuture[]::new))
            .whenComplete((ignored, error) -> bound.removeAll(current));
    }

    private CompletionStage<ZLinkSessionActor> bindBackendRef(
        ZLinkBackendActorRef ref) {
        if (actors != null) {
            Optional<ZLinkActor> localActor = actors.localActor(ref.actorId());
            if (localActor.isPresent()) {
                ZLinkBackendActorRef localRef = actors.refFor(localActor.get());
                if (localRef.nodeRid().equals(ref.nodeRid())
                    && localRef.actorId().equals(ref.actorId())
                    && localRef.generation() == ref.generation()) {
                    return bindManagedAsync(localActor.get());
                }
            }
        }
        return awaitRouteReady(ref)
            .thenCompose(ignored -> stream.bindActor(sessionRid, ref)
                .submit(Duration.ofSeconds(30)))
            .thenApply(ignored -> {
                ZLinkSessionActor actor = new BoundActor(
                    stream,
                    sessionRid,
                    ref,
                    Optional.empty(),
                    actors,
                    serializer,
                    0,
                    routeReady,
                    null,
                    true,
                    defaultCodec);
                bound.add(actor);
                return actor;
            });
    }

    private static boolean sameRef(ZLinkActorRef left, ZLinkActorRef right) {
        return left != null
            && right != null
            && left.actorId().equals(right.actorId())
            && left.nodeRid().equals(right.nodeRid())
            && left.generation() == right.generation();
    }

    CompletionStage<ZLinkSessionActor> bindManagedAsync(ZLinkActor actor) {
        if (actors == null) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "managed actor binding requires an actor runtime"));
        }
        ZLinkBackendActorRef ref = actors.refFor(actor);
        CompletionStage<Void> nativeBinding = nativeSessionRelayAttached
            ? awaitRouteReady(ref)
                .thenCompose(ignored -> stream.bindActor(sessionRid, ref)
                    .submit(Duration.ofSeconds(30)))
            : CompletableFuture.completedFuture(null);
        return nativeBinding
            .thenApply(ignored -> {
                ZLinkBoundSessionRuntime boundSession =
                    new ZLinkBoundSessionRuntime(
                        stream,
                        spotNode,
                        sessionRid,
                        ref.actorId(),
                        serializer,
                        actors,
                        actor,
                        defaultCodec);
                RoutingId sourceNodeRid =
                    nativeSessionRelayAttached ? sessionRid : null;
                RoutingId sourceSessionRid =
                    nativeSessionRelayAttached ? sessionRid : null;
                long bindingToken = actors.bindSession(
                    actor,
                    boundSession,
                    sourceNodeRid,
                    sourceSessionRid);
                boundSession.setBindingToken(bindingToken);
                BoundActor boundActor = new BoundActor(
                    stream,
                    sessionRid,
                    ref,
                    Optional.of(actor),
                    actors,
                    serializer,
                    bindingToken,
                    routeReady,
                    localActorDispatcher,
                    nativeSessionRelayAttached,
                    defaultCodec);
                boundSession.setUnbindListener(() -> bound.remove(boundActor));
                bound.add(boundActor);
                return boundActor;
            });
    }

    private CompletionStage<Void> awaitRouteReady(ZLinkBackendActorRef ref) {
        CompletableFuture<Void> result = new CompletableFuture<>();
        long deadline = System.nanoTime() + RELAY_SUBMIT_TIMEOUT.toNanos();
        class Attempt implements Runnable {
            @Override
            public void run() {
                if (result.isDone()) {
                    return;
                }
                if (routeReady.test(ref.nodeRid())) {
                    result.complete(null);
                    return;
                }
                if (System.nanoTime() >= deadline) {
                    result.completeExceptionally(new TimeoutException(
                        "session relay route was not ready before timeout: "
                            + ref.actorId()));
                    return;
                }
                RELAY_RETRY_EXECUTOR.schedule(this, 10, TimeUnit.MILLISECONDS);
            }
        }
        new Attempt().run();
        return result;
    }

    private static final class BoundActor implements ZLinkSessionActor {
        private final ZLinkBackendStreamSocket stream;
        private final RoutingId sessionRid;
        private final ZLinkBackendActorRef ref;
        private final Optional<ZLinkActor> managedActor;
        private final ZLinkActorRuntime actors;
        private final ZLinkMessageSerializer serializer;
        private final long bindingToken;
        private final Predicate<RoutingId> routeReady;
        private final LocalActorDispatcher localActorDispatcher;
        private final boolean nativeSessionRelayAttached;
        private final ZLinkStreamCodec defaultCodec;

        BoundActor(
            ZLinkBackendStreamSocket stream,
            RoutingId sessionRid,
            ZLinkBackendActorRef ref,
            Optional<ZLinkActor> managedActor,
            ZLinkActorRuntime actors,
            ZLinkMessageSerializer serializer,
            long bindingToken,
            Predicate<RoutingId> routeReady,
            LocalActorDispatcher localActorDispatcher,
            boolean nativeSessionRelayAttached,
            ZLinkStreamCodec defaultCodec) {
            this.stream = stream;
            this.sessionRid = sessionRid;
            this.ref = ref;
            this.managedActor = managedActor;
            this.actors = actors;
            this.serializer = java.util.Objects.requireNonNull(serializer, "serializer");
            this.bindingToken = bindingToken;
            this.routeReady = routeReady == null ? ignored -> true : routeReady;
            this.localActorDispatcher = localActorDispatcher;
            this.nativeSessionRelayAttached = nativeSessionRelayAttached;
            this.defaultCodec = defaultCodec == null ? ZLinkStreamCodec.JSON : defaultCodec;
        }

        @Override
        public String actorId() {
            return ref.actorId();
        }

        @Override
        public ZLinkActorRef ref() {
            return new ZLinkActorRef(ref.nodeRid(), ref.actorId(), ref.generation());
        }

        @Override
        public CompletionStage<Void> relay(ZLinkMessage payload) {
            if (payload == null) {
                return CompletableFuture.failedFuture(new IllegalArgumentException(
                    "payload is required"));
            }
            Optional<ZLinkStreamHeader> currentHeader = currentRelayHeader();
            if (currentHeader.isEmpty()) {
                return CompletableFuture.failedFuture(new IllegalStateException(
                    "Session actor relay requires an active stream dispatch."));
            }
            ZLinkStreamHeader header = currentHeader.get();
            Message message = ZLinkMessagePayloads.message(payload, serializer);
            byte[] payloadBytes = message.toByteArray();
            message.close();
            if (managedActor.isPresent() && localActorDispatcher != null) {
                return relayLocal(header, payloadBytes);
            }
            return awaitRouteReady()
                .thenCompose(ignored -> ensureNativeBinding())
                .thenCompose(ignored -> relayWithRetry(header, payloadBytes));
        }

        private CompletionStage<Void> relayLocal(
            ZLinkStreamHeader header,
            byte[] payloadBytes) {
            if (localActorDispatcher == null) {
                return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                    "local actor dispatch requires a Spot runtime"));
            }
            Message payload = Message.from(payloadBytes);
            return localActorDispatcher.dispatch(ref, header, payload)
                .thenCompose(reply -> {
                    if (reply.isEmpty()) {
                        return CompletableFuture.completedFuture(null);
                    }
                    return replyLocal(header, reply.get());
                })
                .whenComplete((ignored, error) -> payload.close());
        }

        private CompletionStage<Void> replyLocal(
            ZLinkStreamHeader header,
            Message reply) {
            try {
                if (header.requestSequence().isEmpty()) {
                    reply.close();
                    return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                        "actor reply requires a stream request sequence: "
                            + header.packetName()));
                }
                ZLinkStreamHeader replyHeader = new ZLinkStreamHeader(
                    ZLinkStreamMessageKind.RESPONSE,
                    header.codec(),
                    EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
                    header.requestSequence(),
                    header.packetName(),
                    Map.of(),
                    header.correlationId());
                byte[] replyBytes = reply.toByteArray();
                CompletableFuture<Void> result = new CompletableFuture<>();
                long deadline = System.nanoTime() + RELAY_SUBMIT_TIMEOUT.toNanos();
                class Attempt implements Runnable {
                    @Override
                    public void run() {
                        if (result.isDone()) {
                            return;
                        }
                        try (Message attemptReply = Message.from(replyBytes)) {
                            if (stream.reply(
                                sessionRid,
                                replyHeader,
                                List.of(attemptReply),
                                SendFlags.DONT_WAIT)) {
                                result.complete(null);
                                return;
                            }
                        } catch (RuntimeException ex) {
                            result.completeExceptionally(ex);
                            return;
                        }
                        if (System.nanoTime() >= deadline) {
                            result.completeExceptionally(new TimeoutException(
                                "local actor session reply was not ready before timeout: "
                                    + ref.actorId()));
                            return;
                        }
                        RELAY_RETRY_EXECUTOR.schedule(this, 10, TimeUnit.MILLISECONDS);
                    }
                }
                new Attempt().run();
                return result;
            } finally {
                reply.close();
            }
        }

        private CompletionStage<Void> awaitRouteReady() {
            CompletableFuture<Void> result = new CompletableFuture<>();
            long deadline = System.nanoTime() + RELAY_SUBMIT_TIMEOUT.toNanos();
            class Attempt implements Runnable {
                @Override
                public void run() {
                    if (result.isDone()) {
                        return;
                    }
                    if (routeReady.test(ref.nodeRid())) {
                        result.complete(null);
                        return;
                    }
                    if (System.nanoTime() >= deadline) {
                        result.complete(null);
                        return;
                    }
                    RELAY_RETRY_EXECUTOR.schedule(this, 10, TimeUnit.MILLISECONDS);
                }
            }
            new Attempt().run();
            return result;
        }

        private CompletionStage<Void> relayWithRetry(
            ZLinkStreamHeader header,
            byte[] payloadBytes) {
            CompletableFuture<Void> result = new CompletableFuture<>();
            long deadline = System.nanoTime() + RELAY_SUBMIT_TIMEOUT.toNanos();
            class Attempt implements Runnable {
                @Override
                public void run() {
                    if (result.isDone()) {
                        return;
                    }
                    Message payloadPart = Message.from(payloadBytes);
                    try {
                        boolean submitted = stream.relayBoundActor(
                            sessionRid,
                            ref.actorId(),
                            header,
                            List.of(payloadPart),
                            SendFlags.DONT_WAIT);
                        if (submitted) {
                            result.complete(null);
                            return;
                        }
                        if (System.nanoTime() >= deadline) {
                            result.completeExceptionally(new TimeoutException(
                                "session relay route was not ready before timeout: "
                                    + ref.actorId()));
                            return;
                        }
                        retryAfterNativeBinding(this);
                    } catch (ZlinkSubmitException ex) {
                        if (!isRetryableSubmitResult(ex.getResult())) {
                            result.completeExceptionally(ex);
                            return;
                        }
                        if (System.nanoTime() >= deadline) {
                            result.completeExceptionally(new TimeoutException(
                                "session relay route was not ready before timeout: "
                                    + ref.actorId()));
                            return;
                        }
                        retryAfterNativeBinding(this);
                    } catch (RuntimeException ex) {
                        result.completeExceptionally(ex);
                    } finally {
                        payloadPart.close();
                    }
                }
            }
            new Attempt().run();
            return result;
        }

        private void retryAfterNativeBinding(Runnable attempt) {
            stream.bindActor(sessionRid, ref)
                .submit(Duration.ofSeconds(2))
                .whenComplete((ignored, error) ->
                    RELAY_RETRY_EXECUTOR.schedule(attempt, 10, TimeUnit.MILLISECONDS));
        }

        private static boolean isRetryableSubmitResult(SubmitResult result) {
            return result == SubmitResult.NOT_CONNECTED
                || result == SubmitResult.BACKPRESSURED
                || result == SubmitResult.NOT_FOUND;
        }

        private CompletionStage<Void> ensureNativeBinding() {
            CompletableFuture<Void> result = new CompletableFuture<>();
            long deadline = System.nanoTime() + RELAY_SUBMIT_TIMEOUT.toNanos();
            class Attempt implements Runnable {
                @Override
                public void run() {
                    if (result.isDone()) {
                        return;
                    }
                    stream.bindActor(sessionRid, ref)
                        .submit(Duration.ofSeconds(2))
                        .whenComplete((ignored, error) -> {
                            if (error == null || isAlreadyBound(error)) {
                                result.complete(null);
                                return;
                            }
                            if (!isRetriableBindFailure(error)
                                || System.nanoTime() >= deadline) {
                                result.completeExceptionally(error);
                                return;
                            }
                            RELAY_RETRY_EXECUTOR.schedule(this, 10, TimeUnit.MILLISECONDS);
                        });
                }
            }
            new Attempt().run();
            return result;
        }

        @Override
        public CompletionStage<Void> notifyDisconnected() {
            CompletionStage<Void> notification = managedActor
                .map(actor -> actors.clearSessionBinding(actor, bindingToken)
                    ? actors.notifyDisconnected(actor)
                    : CompletableFuture.<Void>completedFuture(null))
                .orElseGet(this::notifyRemoteDisconnected);
            return notification.thenCompose(ignored -> stream.unbindActor(sessionRid, ref.actorId())
                .submit(Duration.ofSeconds(30)));
        }

        private CompletionStage<Void> notifyRemoteDisconnected() {
            if (!nativeSessionRelayAttached) {
                return CompletableFuture.completedFuture(null);
            }
            ZLinkStreamHeader header = new ZLinkStreamHeader(
                ZLinkStreamMessageKind.SEND,
                defaultCodec,
                EnumSet.noneOf(ZLinkStreamHeaderFlag.class),
                Optional.empty(),
                ZLinkActorSpotRoutePackets.SESSION_DISCONNECTED_PACKET_NAME,
                Map.of());
            return awaitRouteReady()
                .thenCompose(ignored -> ensureNativeBinding())
                .thenCompose(ignored -> relayWithRetry(header, new byte[0]));
        }
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
        return request != null
            && (request.getResult() == RequestResult.NOT_CONNECTED
                || request.getResult() == RequestResult.NOT_FOUND
                || request.getResult() == RequestResult.TIMED_OUT);
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
}
