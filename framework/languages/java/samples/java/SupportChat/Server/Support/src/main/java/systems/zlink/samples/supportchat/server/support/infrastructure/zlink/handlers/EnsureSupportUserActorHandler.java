package systems.zlink.samples.supportchat.server.support.infrastructure.zlink.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

@ZLinkHandlerGroup("support")
public final class EnsureSupportUserActorHandler
    implements ZLinkRequestHandler<
        Messages.EnsureSupportUserActorReq,
        Messages.EnsureSupportUserActorRes> {
    private final ZLinkActorManager actors;

    public EnsureSupportUserActorHandler(ZLinkActorManager actors) {
        this.actors = actors;
    }

    @Override
    public Messages.EnsureSupportUserActorRes handle(
        Messages.EnsureSupportUserActorReq request,
        ZLinkRequestContext context) {
        var actor = await(actors.getOrCreate(
            request.actorId(),
            SampleNames.SupportActorType,
            request));
        return new Messages.EnsureSupportUserActorRes(toSnapshot(actor));
    }

    private static Messages.ActorRefSnapshot toSnapshot(ZLinkActorRef actor) {
        return new Messages.ActorRefSnapshot(
            actor.nodeRid().toBytes(),
            actor.actorId(),
            actor.epoch());
    }
}
