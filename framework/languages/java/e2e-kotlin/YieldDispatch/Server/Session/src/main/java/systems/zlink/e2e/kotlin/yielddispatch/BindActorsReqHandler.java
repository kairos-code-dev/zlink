package systems.zlink.e2e.kotlin.yielddispatch;

import java.time.Duration;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class BindActorsReqHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.BindActorsReq> {
    private final ZLinkRouteClient routes;

    public BindActorsReqHandler(ZLinkRouteClient routes) {
        this.routes = routes;
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
                Contracts.SPOT_MESH,
                RoutingId.from("play-a"),
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
        context.client().reply(reply).await();
    }
}
