package systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.entryspot.handlers;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.entryspot.BingoEntrySpot;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class ObserveBingoEventsHandler
    implements ZLinkEntrySpotActorRequestHandler<
        BingoEntrySpot,
        PlayerActor,
        Messages.ObserveBingoEventsReq,
        Messages.ObserveBingoEventsRes> {
    @Override
    public Messages.ObserveBingoEventsRes handle(
        BingoEntrySpot spot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.ObserveBingoEventsReq request,
        CancellationToken cancellationToken) {
        return spot.observeEvents(actor, request);
    }
}
