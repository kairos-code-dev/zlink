package systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions;

import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionActors;
import systems.zlink.framework.streams.ZLinkStreamHeader;

public final class RecordingSessionActors implements ZLinkSessionActors {
    private final List<ZLinkSessionActor> bound = new ArrayList<>();

    @Override
    public List<ZLinkSessionActor> bound() {
        return List.copyOf(bound);
    }

    @Override
    public CompletionStage<ZLinkSessionActor> bindAsync(ZLinkActor actor) {
        return bindAsync(new ZLinkActorRef(RoutingId.from("local-node"), actor.actorId(), 1));
    }

    @Override
    public CompletionStage<ZLinkSessionActor> bindAsync(ZLinkActorRef actor) {
        ZLinkSessionActor sessionActor = new BoundActor(actor);
        bound.add(sessionActor);
        return CompletableFuture.completedFuture(sessionActor);
    }

    @Override
    public Optional<ZLinkSessionActor> find(String actorId) {
        return bound.stream()
            .filter(actor -> actor.actorId().equals(actorId))
            .findFirst();
    }

    private record BoundActor(ZLinkActorRef ref) implements ZLinkSessionActor {
        @Override
        public String actorId() {
            return ref.actorId();
        }

        @Override
        public CompletionStage<Void> relayAsync(ZLinkStreamHeader header, Message payload) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> notifyDisconnectedAsync() {
            return CompletableFuture.completedFuture(null);
        }
    }
}
