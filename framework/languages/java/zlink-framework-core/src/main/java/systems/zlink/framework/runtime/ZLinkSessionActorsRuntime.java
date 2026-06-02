package systems.zlink.framework.runtime;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionActors;
import systems.zlink.framework.streams.ZLinkStreamHeader;

final class ZLinkSessionActorsRuntime implements ZLinkSessionActors {
    private final ZLinkBackendStreamSocket stream;
    private final RoutingId sessionRid;
    private final ZLinkActorRuntime actors;
    private final ZLinkMessageSerializer serializer;
    private final List<ZLinkSessionActor> bound = new ArrayList<>();

    ZLinkSessionActorsRuntime(
        ZLinkBackendStreamSocket stream,
        RoutingId sessionRid,
        ZLinkActorRuntime actors,
        ZLinkMessageSerializer serializer) {
        this.stream = stream;
        this.sessionRid = sessionRid;
        this.actors = actors;
        this.serializer = serializer;
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
        stream.bindActor(sessionRid, ref)
            .submitAsync(Duration.ofSeconds(30))
            .toCompletableFuture()
            .join();
        ZLinkSessionActor actor = new BoundActor(
            stream,
            sessionRid,
            ref,
            Optional.empty(),
            actors,
            0);
        bound.add(actor);
        return CompletableFuture.completedFuture(actor);
    }

    CompletionStage<ZLinkSessionActor> bindManagedAsync(ZLinkActor actor) {
        ZLinkBackendActorRef ref = actors.refFor(actor);
        stream.bindActor(sessionRid, ref)
            .submitAsync(Duration.ofSeconds(30))
            .toCompletableFuture()
            .join();
        ZLinkBoundSessionRuntime boundSession =
            new ZLinkBoundSessionRuntime(stream, sessionRid, ref.actorId(), serializer);
        long bindingToken = actors.bindSession(actor, boundSession);
        ZLinkSessionActor boundActor = new BoundActor(
            stream,
            sessionRid,
            ref,
            Optional.of(actor),
            actors,
            bindingToken);
        bound.add(boundActor);
        return CompletableFuture.completedFuture(boundActor);
    }

    private static final class BoundActor implements ZLinkSessionActor {
        private final ZLinkBackendStreamSocket stream;
        private final RoutingId sessionRid;
        private final ZLinkBackendActorRef ref;
        private final Optional<ZLinkActor> managedActor;
        private final ZLinkActorRuntime actors;
        private final long bindingToken;

        BoundActor(
            ZLinkBackendStreamSocket stream,
            RoutingId sessionRid,
            ZLinkBackendActorRef ref,
            Optional<ZLinkActor> managedActor,
            ZLinkActorRuntime actors,
            long bindingToken) {
            this.stream = stream;
            this.sessionRid = sessionRid;
            this.ref = ref;
            this.managedActor = managedActor;
            this.actors = actors;
            this.bindingToken = bindingToken;
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
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> notifyDisconnectedAsync() {
            stream.unbindActor(sessionRid, ref.actorId())
                .submitAsync(Duration.ofSeconds(30))
                .toCompletableFuture()
                .join();
            managedActor.ifPresent(actor ->
                actors.clearSessionBinding(actor, bindingToken));
            return CompletableFuture.completedFuture(null);
        }
    }
}
