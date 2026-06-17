package systems.zlink.samples.supportchat.server.support.adapters.zlink.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.server.configuration.SampleTimings;
import systems.zlink.samples.supportchat.server.configuration.SampleTopology;
import systems.zlink.samples.supportchat.server.support.adapters.zlink.actors.SupportActorDirectory;
import systems.zlink.samples.supportchat.server.support.adapters.zlink.actors.SupportUserActor;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

@ZLinkHandlerGroup("support")
public final class EnsureSupportUserActorHandler
    implements ZLinkRequestHandler<
        Messages.EnsureSupportUserActorReq,
        Messages.EnsureSupportUserActorRes> {
    private final ZLinkActorManager actors;
    private final SupportActorDirectory directory;

    public EnsureSupportUserActorHandler(
        ZLinkActorManager actors,
        SupportActorDirectory directory) {
        this.actors = actors;
        this.directory = directory;
    }

    @Override
    public Messages.EnsureSupportUserActorRes handle(
        Messages.EnsureSupportUserActorReq request,
        ZLinkRequestContext context) {
        var actor = await(actors.getOrCreate(request.actorId(), SampleNames.SupportActorType));
        if (!(actor instanceof SupportUserActor supportActor)) {
            throw new IllegalStateException("Support actor factory returned an unexpected actor type.");
        }
        supportActor.setIdentity(request.displayName(), request.role());
        directory.addOrUpdate(supportActor);
        var joined = actor.context()
            .joinEntrySpot(RoutingId.from(SampleTopology.SupportRid), Message.from(new byte[0]))
            .timeout(SampleTimings.RequestTimeout)
            .await(Message.class);
        if (!supportActor.conversationId().isBlank()) {
            actor.context()
                .joinSpot(
                    RoutingId.from(supportActor.conversationId()),
                    new Messages.JoinConversationReq(supportActor.conversationId()))
                .timeout(SampleTimings.RequestTimeout)
                .await(Messages.JoinConversationRes.class);
        }
        return new Messages.EnsureSupportUserActorRes(toSnapshot(joined.actor()));
    }

    private static Messages.ActorRefSnapshot toSnapshot(ZLinkActorRef actor) {
        return new Messages.ActorRefSnapshot(
            actor.nodeRid().toBytes(),
            actor.actorId(),
            actor.epoch());
    }
}
