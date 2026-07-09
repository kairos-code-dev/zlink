package systems.zlink.e2e.yielddispatch.shared;

import java.util.List;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkSpotManager;

public final class PlayBindActorsHandler
    implements ZLinkRouteRequestHandler<Contracts.BindActorsReq, Contracts.BindActorsRes> {
    private final ZLinkActorManager actors;
    private final ZLinkSpotManager spots;
    private final EvidenceStore evidence;

    public PlayBindActorsHandler(
        ZLinkActorManager actors,
        ZLinkSpotManager spots,
        EvidenceStore evidence) {
        this.actors = actors;
        this.spots = spots;
        this.evidence = evidence;
    }

    @Override
    public Contracts.BindActorsRes handle(
        Contracts.BindActorsReq request,
        ZLinkRouteRequestContext context) {
        spots.getOrCreate(
                YieldProbeSpot.class,
                RoutingId.from(request.spotRid()),
                ZLinkMessage.of("bind"))
            .toCompletableFuture()
            .join();
        ActorRef actorA = bind(request.spotRid(), request.actorA());
        ActorRef actorB = bind(request.spotRid(), request.actorB());
        return new Contracts.BindActorsRes(
            request.spotRid(),
            request.actorA(),
            request.actorB(),
            List.of(binding(actorA), binding(actorB)));
    }

    private ActorRef bind(String spotRid, String actorId) {
        ActorRef actor = actors.getOrCreate(actorId, Contracts.ACTOR_TYPE)
            .toCompletableFuture()
            .join();
        evidence.record("bind-actor", spotRid, "actor=" + actor.actorId() + ";node=" + actor.nodeRid());
        return actor;
    }

    private static Contracts.ActorBinding binding(ActorRef actor) {
        return new Contracts.ActorBinding(
            actor.actorId(),
            actor.nodeRid().toString(),
            actor.generation());
    }
}
