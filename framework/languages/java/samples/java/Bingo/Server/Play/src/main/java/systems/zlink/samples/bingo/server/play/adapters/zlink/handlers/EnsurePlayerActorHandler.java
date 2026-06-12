package systems.zlink.samples.bingo.server.play.adapters.zlink.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.bingo.server.play.adapters.zlink.actors.PlayerActor;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.configuration.SampleTimings;
import systems.zlink.samples.bingo.server.configuration.SampleTopology;
import systems.zlink.samples.bingo.shared.contracts.Messages;

@ZLinkHandlerGroup("play")
public final class EnsurePlayerActorHandler
    implements ZLinkRequestHandler<
        Messages.EnsurePlayerActorReq,
        Messages.EnsurePlayerActorRes> {
    private final ZLinkActorManager actors;

    public EnsurePlayerActorHandler(ZLinkActorManager actors) {
        this.actors = actors;
    }

    @Override
    public Messages.EnsurePlayerActorRes handle(
        Messages.EnsurePlayerActorReq request,
        ZLinkRequestContext context) {
        var actor = await(actors.getOrCreate(request.actorId(), SampleNames.PlayerActorType));
        if (actor instanceof PlayerActor player) {
            player.setDisplayName(request.displayName());
        }
        var joined = actor.context()
            .joinEntrySpot(RoutingId.from(SampleTopology.PlayRid))
            .timeout(SampleTimings.RequestTimeout)
            .await();
        return new Messages.EnsurePlayerActorRes(
            request.actorId(),
            SampleNames.PlayerActorType,
            toSnapshot(joined));
    }

    private static Messages.ActorRefSnapshot toSnapshot(ZLinkActorRef actor) {
        return new Messages.ActorRefSnapshot(
            actor.nodeRid().toBytes(),
            actor.actorId(),
            actor.epoch());
    }
}
