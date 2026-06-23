package systems.zlink.samples.bingo.server.play.infrastructure.zlink.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.channels.ZLinkRouteRequestContext;
import systems.zlink.framework.channels.ZLinkRouteRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.actors.PlayerActor;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.configuration.SampleTimings;
import systems.zlink.samples.bingo.server.configuration.SampleTopology;
import systems.zlink.samples.bingo.shared.contracts.Messages;

@ZLinkHandlerGroup("play-route")
public final class EnsurePlayerActorHandler
    implements ZLinkRouteRequestHandler<
        Messages.EnsurePlayerActorReq,
        Messages.EnsurePlayerActorRes> {
    private final ZLinkActorManager actors;

    public EnsurePlayerActorHandler(ZLinkActorManager actors) {
        this.actors = actors;
    }

    @Override
    public Messages.EnsurePlayerActorRes handle(
        Messages.EnsurePlayerActorReq request,
        ZLinkRouteRequestContext context) {
        if (request.preferredActorNodeRid() != null
            && !request.preferredActorNodeRid().isBlank()
            && !request.preferredActorNodeRid().equals(SampleTopology.selectedPlayNodeRid())) {
            throw new IllegalStateException("EnsurePlayerActor reached the wrong Play node.");
        }
        var actor = await(actors.getOrCreate(request.actorId(), SampleNames.PlayerActorType));
        if (actor instanceof PlayerActor player) {
            player.setDisplayName(request.displayName());
        }
        var joined = actor.context()
            .joinEntrySpot(RoutingId.from(SampleTopology.selectedPlayNodeRid()))
            .timeout(SampleTimings.RequestTimeout)
            .await();
        return new Messages.EnsurePlayerActorRes(
            request.actorId(),
            SampleNames.PlayerActorType,
            toSnapshot(joined.actor()));
    }

    private static Messages.ActorRefSnapshot toSnapshot(ZLinkActorRef actor) {
        return new Messages.ActorRefSnapshot(
            actor.nodeRid().toBytes(),
            actor.actorId(),
            actor.epoch());
    }
}
