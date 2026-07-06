package systems.zlink.samples.bingo.server.play.infrastructure.zlink.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.actors.ZLinkActorRefSnapshot;
import systems.zlink.framework.spots.ZLinkSpotRequestHandler;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.entryspot.BingoEntrySpot;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class EnsurePlayerActorHandler
    implements ZLinkSpotRequestHandler<BingoEntrySpot, Messages.EnsurePlayerActorReq, Messages.EnsurePlayerActorRes> {
    private final ZLinkActorManager actors;

    public EnsurePlayerActorHandler(ZLinkActorManager actors) {
        this.actors = actors;
    }

    @Override
    public Messages.EnsurePlayerActorRes handle(
        BingoEntrySpot spot,
        Messages.EnsurePlayerActorReq request) {
        ZLinkActorRef actor = await(actors.getOrCreate(
            request.actorId(),
            SampleNames.PlayerActorType,
            request));
        return new Messages.EnsurePlayerActorRes(
            request.actorId(),
            SampleNames.PlayerActorType,
            ZLinkActorRefSnapshot.from(actor));
    }
}
