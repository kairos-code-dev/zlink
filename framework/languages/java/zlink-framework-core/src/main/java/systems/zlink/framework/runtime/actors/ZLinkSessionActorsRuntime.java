package systems.zlink.framework.runtime.actors;

import systems.zlink.framework.runtime.backend.*;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
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
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionActors;
import systems.zlink.framework.streams.ZLinkStreamHeader;

public final class ZLinkSessionActorsRuntime implements ZLinkSessionActors {
    private static final Duration RELAY_SUBMIT_TIMEOUT = Duration.ofSeconds(30);
    private static final ScheduledExecutorService RELAY_RETRY_EXECUTOR =
        Executors.newSingleThreadScheduledExecutor(task -> {
            Thread thread = new Thread(task, "zlink-java-session-actor-relay");
            thread.setDaemon(true);
            return thread;
        });

    private final ZLinkBackendStreamSocket stream;
    private final RoutingId sessionRid;
    private final ZLinkActorRuntime actors;
    private final ZLinkMessageSerializer serializer;
    private final Predicate<RoutingId> routeReady;
    private final List<ZLinkSessionActor> bound = new ArrayList<>();

    public ZLinkSessionActorsRuntime(
        ZLinkBackendStreamSocket stream,
        RoutingId sessionRid,
        ZLinkActorRuntime actors,
        ZLinkMessageSerializer serializer) {
        this(stream, sessionRid, actors, serializer, ignored -> true);
    }

    public ZLinkSessionActorsRuntime(
        ZLinkBackendStreamSocket stream,
        RoutingId sessionRid,
        ZLinkActorRuntime actors,
        ZLinkMessageSerializer serializer,
        Predicate<RoutingId> routeReady) {
        this.stream = stream;
        this.sessionRid = sessionRid;
        this.actors = actors;
        this.serializer = serializer;
        this.routeReady = routeReady == null ? ignored -> true : routeReady;
    }

    @Override
    public List<ZLinkSessionActor> bound() {
        return List.copyOf(bound);
    }

    @Override
    public CompletionStage<ZLinkSessionActor> bindAsync(ZLinkActor actor) {
        return bindManagedAsync(actor);
    }

    @Override
    public CompletionStage<ZLinkSessionActor> bindAsync(ZLinkActorRef actor) {
        ZLinkBackendActorRef ref = new ZLinkBackendActorRef(
            actor.nodeRid(),
            actor.actorId(),
            actor.epoch());
        return bindBackendRef(ref);
    }

    @Override
    public Optional<ZLinkSessionActor> find(String actorId) {
        return bound.stream()
            .filter(actor -> actor.actorId().equals(actorId))
            .findFirst();
    }

    private CompletionStage<ZLinkSessionActor> bindBackendRef(
        ZLinkBackendActorRef ref) {
        return awaitRouteReady(ref)
            .thenCompose(ignored -> stream.bindActor(sessionRid, ref)
                .submitAsync(Duration.ofSeconds(30)))
            .thenApply(ignored -> {
                ZLinkSessionActor actor = new BoundActor(
                    stream,
                    sessionRid,
                    ref,
                    Optional.empty(),
                    actors,
                    0,
                    routeReady);
                bound.add(actor);
                return actor;
            });
    }

    CompletionStage<ZLinkSessionActor> bindManagedAsync(ZLinkActor actor) {
        if (actors == null) {
            return CompletableFuture.failedFuture(new ZLinkConfigurationException(
                "managed actor binding requires an actor runtime"));
        }
        ZLinkBackendActorRef ref = actors.refFor(actor);
        return awaitRouteReady(ref)
            .thenCompose(ignored -> stream.bindActor(sessionRid, ref)
                .submitAsync(Duration.ofSeconds(30)))
            .thenApply(ignored -> {
                ZLinkBoundSessionRuntime boundSession =
                    new ZLinkBoundSessionRuntime(
                        stream,
                        sessionRid,
                        ref.actorId(),
                        serializer,
                        actors,
                        actor);
                long bindingToken = actors.bindSession(actor, boundSession);
                boundSession.setBindingToken(bindingToken);
                ZLinkSessionActor boundActor = new BoundActor(
                    stream,
                    sessionRid,
                    ref,
                    Optional.of(actor),
                    actors,
                    bindingToken,
                    routeReady);
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
                        "ActorGateway route was not ready before timeout: "
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
        private final long bindingToken;
        private final Predicate<RoutingId> routeReady;

        BoundActor(
            ZLinkBackendStreamSocket stream,
            RoutingId sessionRid,
            ZLinkBackendActorRef ref,
            Optional<ZLinkActor> managedActor,
            ZLinkActorRuntime actors,
            long bindingToken,
            Predicate<RoutingId> routeReady) {
            this.stream = stream;
            this.sessionRid = sessionRid;
            this.ref = ref;
            this.managedActor = managedActor;
            this.actors = actors;
            this.bindingToken = bindingToken;
            this.routeReady = routeReady == null ? ignored -> true : routeReady;
        }

        @Override
        public String actorId() {
            return ref.actorId();
        }

        @Override
        public ZLinkActorRef ref() {
            return new ZLinkActorRef(ref.nodeRid(), ref.actorId(), ref.epoch());
        }

        @Override
        public CompletionStage<Void> relayAsync(
            ZLinkStreamHeader header,
            Message payload) {
            if (header == null) {
                return CompletableFuture.failedFuture(new IllegalArgumentException(
                    "header is required"));
            }
            if (payload == null) {
                return CompletableFuture.failedFuture(new IllegalArgumentException(
                    "payload is required"));
            }
            byte[] payloadBytes = payload.toByteArray();
            return awaitRouteReady()
                .thenCompose(ignored -> ensureNativeBinding())
                .thenCompose(ignored -> relayWithRetry(header, payloadBytes));
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
                        result.completeExceptionally(new TimeoutException(
                            "ActorGateway route was not ready before timeout: "
                                + ref.actorId()));
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
                                "ActorGateway route was not ready before timeout: "
                                    + ref.actorId()));
                            return;
                        }
                        RELAY_RETRY_EXECUTOR.schedule(this, 10, TimeUnit.MILLISECONDS);
                    } catch (ZlinkSubmitException ex) {
                        if (ex.getResult() != SubmitResult.NOT_CONNECTED) {
                            result.completeExceptionally(ex);
                            return;
                        }
                        if (System.nanoTime() >= deadline) {
                            result.completeExceptionally(new TimeoutException(
                                "ActorGateway route was not ready before timeout: "
                                    + ref.actorId()));
                            return;
                        }
                        RELAY_RETRY_EXECUTOR.schedule(this, 10, TimeUnit.MILLISECONDS);
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
                        .submitAsync(Duration.ofSeconds(2))
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
        public CompletionStage<Void> notifyDisconnectedAsync() {
            return stream.unbindActor(sessionRid, ref.actorId())
                .submitAsync(Duration.ofSeconds(30))
                .thenCompose(ignored -> managedActor
                    .map(actor -> actors.clearSessionBinding(actor, bindingToken)
                        ? actors.notifyDisconnected(actor)
                        : CompletableFuture.<Void>completedFuture(null))
                    .orElseGet(() -> CompletableFuture.completedFuture(null)));
        }
    }

    private static boolean isAlreadyBound(Throwable error) {
        ZlinkRequestException request = findRequestException(error);
        return request != null
            && (request.getResult() == RequestResult.CONFLICT
                || request.getResult() == RequestResult.BUSY
                || request.getInternalErrno() == 16);
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
