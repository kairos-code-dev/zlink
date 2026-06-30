package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionDispatchContext;
import systems.zlink.framework.streams.ZLinkTypedSessionPacketHandler;

public final class ActorAuthHandler
    implements ZLinkTypedSessionPacketHandler<ZLinkSessionContext, Contracts.ActorAuthRequest> {
    private final ZLinkActorManager actors;

    public ActorAuthHandler(ZLinkActorManager actors) {
        this.actors = actors;
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
        var actor = actors.getOrCreate(request.actorId(), "probe", request)
            .toCompletableFuture()
            .join();
        context.actors().bind(actor).toCompletableFuture().join();
        context.client().reply(new Contracts.ActorAuthReply(request.actorId())).await();
    }
}
