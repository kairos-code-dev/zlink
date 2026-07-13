package systems.zlink.samples.bingo.server.play.infrastructure.zlink.handlers;


import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.spots.ZLinkSpotRequestHandler;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.entryspot.BingoEntrySpot;
import systems.zlink.samples.bingo.shared.contracts.BingoMessages;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class EnsurePlayerActorHandler
    implements ZLinkSpotRequestHandler<BingoEntrySpot, Messages.EnsurePlayerActorReq, Messages.EnsurePlayerActorRes> {
    private final ZLinkActorManager actors;

    public EnsurePlayerActorHandler(ZLinkActorManager actors) {
        this.actors = actors;
    }

    @Override
    public java.util.concurrent.CompletionStage<Messages.EnsurePlayerActorRes> handle(
        BingoEntrySpot spot,
        Messages.EnsurePlayerActorReq request) {
        return actors.getOrCreate(
            request.getActorId(),
            SampleNames.PlayerActorType,
            request).thenApply(actor -> BingoMessages.ensurePlayerActorRes(
                request.getActorId(),
                SampleNames.PlayerActorType,
                BingoMessages.actorRefWire(
                    actor.nodeRid().toString(),
                    actor.actorId(),
                    actor.generation())));
    }
}
