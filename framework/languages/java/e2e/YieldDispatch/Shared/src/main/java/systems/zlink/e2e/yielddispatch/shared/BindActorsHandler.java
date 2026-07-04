package systems.zlink.e2e.yielddispatch.shared;

import java.time.Duration;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class BindActorsHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.BindActorsReq> {
    private final ZLinkRouteClient routes;
    private final EvidenceStore evidence;

    public BindActorsHandler(
        ZLinkRouteClient routes,
        EvidenceStore evidence) {
        this.routes = routes;
        this.evidence = evidence;
    }

    @Override
    public String packetName() {
        return "BindActorsReq";
    }

    @Override
    public Class<Contracts.BindActorsReq> messageType() {
        return Contracts.BindActorsReq.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Contracts.BindActorsReq request) {
        Contracts.BindActorsRes reply = routes
            .requestToNode(
                Contracts.ROUTE_CHANNEL,
                RoutingId.from(Contracts.PLAY_NODE),
                request)
            .timeout(Duration.ofSeconds(30))
            .await(Contracts.BindActorsRes.class);
        for (Contracts.ActorBinding actor : reply.actors()) {
            context.actors().bind(new ZLinkActorRef(
                    RoutingId.from(actor.nodeRid()),
                    actor.actorId(),
                    actor.generation()))
                .toCompletableFuture()
                .join();
        }
        evidence.record("actors-bound", request.spotRid(), request.actorA() + "," + request.actorB());
        context.client()
            .reply(reply)
            .await();
    }
}
