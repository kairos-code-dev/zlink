package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class ActorAuthReqHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.ActorAuthReq> {
    private final ZLinkActorManager actors;

    public ActorAuthReqHandler(ZLinkActorManager actors) {
        this.actors = actors;
    }

    @Override
    public String packetName() {
        return "ActorAuthReq";
    }

    @Override
    public Class<Contracts.ActorAuthReq> messageType() {
        return Contracts.ActorAuthReq.class;
    }

    @Override
    public void handle(
        ZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Contracts.ActorAuthReq request) {
        var actor = actors.getOrCreate(request.actorId(), "probe", request)
            .toCompletableFuture()
            .join();
        context.actors().bind(actor).toCompletableFuture().join();
        context.client().reply(new Contracts.ActorAuthRes(request.actorId())).await();
    }
}
