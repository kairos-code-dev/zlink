package systems.zlink.e2e.spotservice.shared;

import java.util.List;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class MultiBindHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.MultiBindReq> {
    private final ZLinkActorManager actors;
    private final ScenarioState evidence;

    public MultiBindHandler(
        ZLinkActorManager actors,
        ScenarioState evidence) {
        this.actors = actors;
        this.evidence = evidence;
    }

    @Override
    public String packetName() {
        return "MultiBindReq";
    }

    @Override
    public Class<Contracts.MultiBindReq> messageType() {
        return Contracts.MultiBindReq.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Contracts.MultiBindReq request) {
        for (String actorId : List.of(request.firstActorId(), request.secondActorId())) {
            var actor = actors.getOrCreate(
                    actorId,
                    "scenario",
                    new Contracts.ActorAuthReq(actorId, request.profile()))
                .toCompletableFuture()
                .join();
            context.actors().bind(actor)
                .toCompletableFuture()
                .join();
            evidence.record("ActorSessionBound", "session", actorId);
        }
        context.client()
            .reply(new Contracts.MultiBindRes(context.actors().bound().size()))
            .await();
    }
}
