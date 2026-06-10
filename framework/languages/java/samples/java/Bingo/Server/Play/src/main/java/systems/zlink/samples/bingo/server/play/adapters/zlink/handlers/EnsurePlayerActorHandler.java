package systems.zlink.samples.bingo.server.play.adapters.zlink.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.bingo.server.play.adapters.zlink.actors.PlayerActor;
import systems.zlink.samples.bingo.shared.configuration.SampleNames;
import systems.zlink.samples.bingo.shared.configuration.SampleTimings;
import systems.zlink.samples.bingo.shared.configuration.SampleTopology;
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
    public CompletionStage<Messages.EnsurePlayerActorRes> handleAsync(
        Messages.EnsurePlayerActorReq request,
        ZLinkRequestContext context) {
        return actors.getOrCreateAsync(request.actorId(), SampleNames.PlayerActorType)
            .thenCompose(actor -> {
                if (actor instanceof PlayerActor player) {
                    player.setDisplayName(request.displayName());
                }
                return actor.context()
                    .joinEntrySpot(RoutingId.from(SampleTopology.PlayRid))
                    .timeout(SampleTimings.RequestTimeout)
                    .submit();
            })
            .thenApply(joined -> new Messages.EnsurePlayerActorRes(
                request.actorId(),
                SampleNames.PlayerActorType,
                toSnapshot(joined)));
    }

    private static Messages.ActorRefSnapshot toSnapshot(ZLinkActorRef actor) {
        return new Messages.ActorRefSnapshot(
            actor.nodeRid().toBytes(),
            actor.actorId(),
            actor.epoch());
    }
}
