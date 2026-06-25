package systems.zlink.e2e.kotlin.spotservice;

import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class ActorAuthHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.ActorAuthRequest> {
    private final ZLinkActorManager actors;
    private final ScenarioState evidence;

    public ActorAuthHandler(
        ZLinkActorManager actors,
        ScenarioState evidence) {
        this.actors = actors;
        this.evidence = evidence;
    }

    @Override
    public String packetName() {
        return "ActorAuthRequest";
    }

    @Override
    public Class<Contracts.ActorAuthRequest> messageType() {
        return Contracts.ActorAuthRequest.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Contracts.ActorAuthRequest request) {
        var actor = actors.getOrCreate(request.actorId(), "scenario", request)
            .toCompletableFuture()
            .join();
        var bound = context.actors().bind(actor)
            .toCompletableFuture()
            .join();
        evidence.record("ActorSessionBound", "session", request.actorId());
        context.client()
            .reply(new Contracts.ActorAuthReply(
                bound.actorId(),
                bound.ref().nodeRid().toString(),
                context.actors().bound().size(),
                request.profile().displayName(),
                request.profile().level(),
                request.profile().tags()))
            .await();
    }
}
