package systems.zlink.samples.supportchat.server.support.handlers;

import systems.zlink.framework.actors.ActorRefSnapshot;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

@ZLinkHandlerGroup(SampleNames.SupportChannel)
public final class EnsureSupportUserActorHandler
    implements ZLinkRequestHandler<Messages.EnsureSupportUserActorReq, Messages.EnsureSupportUserActorRes> {
    private final ZLinkActorManager actors;

    public EnsureSupportUserActorHandler(ZLinkActorManager actors) {
        this.actors = actors;
    }

    @Override
    public java.util.concurrent.CompletionStage<Messages.EnsureSupportUserActorRes> handle(
        Messages.EnsureSupportUserActorReq request,
        ZLinkRequestContext context) {
        return actors.getOrCreate(request.actorId(), SampleNames.SupportActorType, request)
            .thenApply(actor -> new Messages.EnsureSupportUserActorRes(ActorRefSnapshot.from(actor)));
    }
}
